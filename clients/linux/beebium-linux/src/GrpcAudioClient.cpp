#include "GrpcAudioClient.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <QAudioSink>
#include <QAudioDevice>
#include <QDebug>
#include <QMediaDevices>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

AudioBufferDevice::AudioBufferDevice(QObject *parent)
    : QIODevice(parent)
    , mutex_(std::make_unique<QMutex>()) {
}

void AudioBufferDevice::start() {
    open(QIODevice::ReadOnly);
}

void AudioBufferDevice::stop() {
    close();
}

void AudioBufferDevice::resetBuffer() {
    QMutexLocker locker(mutex_.get());
    buffer_.clear();
}

void AudioBufferDevice::pushPcm(const QByteArray &pcm) {
    QMutexLocker locker(mutex_.get());
    const bool wasEmpty = buffer_.isEmpty();
    buffer_.append(pcm);
    if (buffer_.size() > maxBytes_) {
        buffer_.remove(0, buffer_.size() - maxBytes_);
    }
    locker.unlock();
    if (wasEmpty && isOpen()) {
        emit readyRead();
    }
}

bool AudioBufferDevice::isSequential() const {
    return true;
}

qint64 AudioBufferDevice::bytesAvailable() const {
    QMutexLocker locker(mutex_.get());
    return buffer_.size() + QIODevice::bytesAvailable();
}

qint64 AudioBufferDevice::readData(char *data, qint64 maxSize) {
    QMutexLocker locker(mutex_.get());
    const qint64 available = std::min<qint64>(maxSize, buffer_.size());
    if (available > 0) {
        std::memcpy(data, buffer_.constData(), static_cast<size_t>(available));
        buffer_.remove(0, static_cast<qsizetype>(available));
    }
    if (available < maxSize) {
        std::memset(data + available, 0, static_cast<size_t>(maxSize - available));
    }
    return maxSize;
}

qint64 AudioBufferDevice::writeData(const char *, qint64) {
    return -1;
}

GrpcAudioClient::GrpcAudioClient(QObject *parent)
    : QObject(parent)
    , device_(std::make_unique<AudioBufferDevice>())
    , pumpTimer_(std::make_unique<QTimer>()) {
    pumpTimer_->setInterval(5);
    connect(pumpTimer_.get(), &QTimer::timeout, this, &GrpcAudioClient::pumpAudio);
}

GrpcAudioClient::~GrpcAudioClient() {
    disconnectFromTarget();
}

void GrpcAudioClient::connectToTarget(const ConnectionTarget &target) {
    disconnectFromTarget();
    running_.store(true);
    worker_ = std::thread([this, target] { streamLoop(target); });
}

void GrpcAudioClient::disconnectFromTarget() {
    running_.store(false);
    if (streamContext_) {
        streamContext_->TryCancel();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    streamContext_.reset();
    channel_.reset();

    invokeOnOwnerThread([this]() {
        if (pumpTimer_) {
            pumpTimer_->stop();
        }
        if (sink_) {
            sink_->stop();
            sink_.reset();
        }
        sinkDevice_ = nullptr;
        device_->stop();
        device_->resetBuffer();
        pendingPcm_.clear();
        receivedChunks_ = 0;
        droppedChunks_ = 0;
        lastChunkSequence_ = 0;
        haveLastChunkSequence_ = false;
    }, true);
}

QString GrpcAudioClient::audioFormatSummary() const {
    if (!outputFormat_.isValid()) {
        return tr("No audio format negotiated");
    }
    return tr("%1 Hz, %2 ch, sample format %3")
        .arg(outputFormat_.sampleRate())
        .arg(outputFormat_.channelCount())
        .arg(static_cast<int>(outputFormat_.sampleFormat()));
}

QString GrpcAudioClient::audioDeviceSummary() const {
    return outputDeviceDescription_.isEmpty() ? tr("Default output") : outputDeviceDescription_;
}

QString GrpcAudioClient::audioStreamSummary() const {
    return tr("chunks %1, gaps %2, queued %3 bytes")
        .arg(receivedChunks_)
        .arg(droppedChunks_)
        .arg(pendingPcm_.size());
}

QStringList GrpcAudioClient::availableOutputDevices() const {
    QStringList devices;
    const auto outputs = QMediaDevices::audioOutputs();
    devices.reserve(outputs.size());
    for (const QAudioDevice &device : outputs) {
        devices.push_back(device.description());
    }
    return devices;
}

QString GrpcAudioClient::preferredOutputDeviceDescription() const {
    return preferredOutputDeviceDescription_;
}

void GrpcAudioClient::setPreferredOutputDeviceDescription(const QString &description) {
    preferredOutputDeviceDescription_ = description;
}

void GrpcAudioClient::setVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    const float appliedVolume = std::pow(volume_, 2.5f);
    if (sink_) {
        sink_->setVolume(appliedVolume);
    }
}

float GrpcAudioClient::volume() const {
    return volume_;
}

void GrpcAudioClient::streamLoop(ConnectionTarget target) {
    channel_ = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    auto stub = beebium::AudioService::NewStub(channel_);

    grpc::ClientContext formatContext;
    beebium::GetAudioFormatRequest formatRequest;
    beebium::AudioFormat formatResponse;
    const grpc::Status formatStatus = stub->GetAudioFormat(&formatContext, formatRequest, &formatResponse);
    if (!formatStatus.ok()) {
        emit errorOccurred(tr("GetAudioFormat failed: %1").arg(QString::fromStdString(formatStatus.error_message())));
        return;
    }

    sampleRate_ = formatResponse.sample_rate();
    sourceCount_ = formatResponse.source_count();
    if (formatResponse.sources_size() > 0) {
        sourceEncoding_ = formatResponse.sources(0).encoding();
    }

    invokeOnOwnerThread([this]() { configureAudioSink(sampleRate_); }, true);

    streamContext_ = std::make_unique<grpc::ClientContext>();
    beebium::SubscribeAudioRequest request;
    request.set_chunk_size(256);
    std::unique_ptr<grpc::ClientReader<beebium::AudioChunk>> reader(
        stub->SubscribeAudio(streamContext_.get(), request));

    beebium::AudioChunk chunk;
    while (running_.load() && reader->Read(&chunk)) {
        const QByteArray pcm = decodeChunk(chunk);
        if (!pcm.isEmpty()) {
            invokeOnOwnerThread([this, pcm, sequence = static_cast<quint64>(chunk.sequence())]() {
                enqueuePcm(pcm, sequence);
            }, false);
        }
    }

    const grpc::Status status = reader->Finish();
    if (running_.load() && !status.ok()) {
        const QString details = QString::fromStdString(status.error_message());
        if (details.contains("Connection reset by peer", Qt::CaseInsensitive)) {
            emit statusChanged(tr("Audio stream disconnected"));
        } else {
            emit errorOccurred(tr("Audio stream ended: %1").arg(details));
        }
    }
}

QByteArray GrpcAudioClient::decodeChunk(const beebium::AudioChunk &chunk) const {
    const int sampleCount = static_cast<int>(chunk.sample_count());
    const int fieldCount = static_cast<int>(sourceCount_);
    const int bytesPerSample = fieldCount * 4;
    const auto &sourceBytes = chunk.samples();
    if (sampleCount <= 0 || fieldCount <= 0 || static_cast<int>(sourceBytes.size()) < sampleCount * bytesPerSample) {
        return {};
    }

    if (!outputFormat_.isValid()) {
        return {};
    }

    QByteArray pcm;
    pcm.reserve(sampleCount * outputFormat_.bytesPerFrame());

    for (int i = 0; i < sampleCount; ++i) {
        const char *sampleBase = sourceBytes.data() + i * bytesPerSample;
        float left = 0.0f;
        float right = 0.0f;

        if (sourceEncoding_ == beebium::ENCODING_4X8BIT_UNSIGNED) {
            std::array<unsigned char, 4> channels{};
            std::memcpy(channels.data(), sampleBase, 4);
            float mixed = 0.0f;
            for (unsigned char ch : channels) {
                mixed += (static_cast<int>(ch) - 128) / 127.0f;
            }
            mixed *= 0.75f;
            left = mixed;
            right = mixed;
        } else if (sourceEncoding_ == beebium::ENCODING_2X16BIT_SIGNED) {
            std::array<qint16, 2> stereo{};
            std::memcpy(stereo.data(), sampleBase, 4);
            left = static_cast<float>(stereo[0]) / 32768.0f;
            right = static_cast<float>(stereo[1]) / 32768.0f;
        } else if (sourceEncoding_ == beebium::ENCODING_1X32BIT_SIGNED) {
            qint32 mono = 0;
            std::memcpy(&mono, sampleBase, 4);
            const float monoFloat = std::clamp(static_cast<double>(mono) / 2147483648.0, -1.0, 1.0);
            left = static_cast<float>(monoFloat);
            right = static_cast<float>(monoFloat);
        }

        pcm.append(packStereoSample(left, right));
    }

    return pcm;
}

void GrpcAudioClient::configureAudioSink(quint32 sampleRate) {
    if (sink_) {
        sink_->stop();
        sink_.reset();
    }

    device_->stop();
    device_->resetBuffer();

    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (!preferredOutputDeviceDescription_.isEmpty()) {
        const auto outputs = QMediaDevices::audioOutputs();
        const auto it = std::find_if(outputs.begin(), outputs.end(), [this](const QAudioDevice &device) {
            return device.description() == preferredOutputDeviceDescription_;
        });
        if (it != outputs.end()) {
            outputDevice = *it;
        }
    }
    outputDeviceDescription_ = outputDevice.description();

    QAudioFormat preferred = outputDevice.preferredFormat();
    const QList<QAudioFormat::SampleFormat> candidates = {
        QAudioFormat::Int16,
        QAudioFormat::Float,
        preferred.sampleFormat(),
        QAudioFormat::Int32,
        QAudioFormat::UInt8
    };

    QAudioFormat selectedFormat;
    for (QAudioFormat::SampleFormat candidate : candidates) {
        if (candidate == QAudioFormat::Unknown) {
            continue;
        }
        QAudioFormat format;
        format.setSampleRate(static_cast<int>(sampleRate));
        format.setChannelCount(2);
        format.setSampleFormat(candidate);
        if (outputDevice.isFormatSupported(format)) {
            selectedFormat = format;
            break;
        }
    }

    if (!selectedFormat.isValid()) {
        if (preferred.channelCount() >= 2) {
            selectedFormat = preferred;
        } else {
            selectedFormat = preferred;
            selectedFormat.setChannelCount(2);
        }
    }

    sink_ = std::make_unique<QAudioSink>(outputDevice, selectedFormat, this);
    outputFormat_ = selectedFormat;
    sink_->setBufferSize(static_cast<int>(sampleRate) / 8 * selectedFormat.bytesPerFrame());
    sink_->setVolume(std::pow(volume_, 2.5f));
    connect(sink_.get(), &QAudioSink::stateChanged, this, [this](QtAudio::State state) {
        const QString status = [this, state]() {
            switch (state) {
            case QtAudio::IdleState:
                return tr("Audio idle (%1, %2)").arg(audioFormatSummary(), audioStreamSummary());
            case QtAudio::SuspendedState:
                return tr("Audio suspended");
            case QtAudio::StoppedState:
                return tr("Audio stopped (%1)").arg(audioStreamSummary());
            case QtAudio::ActiveState:
                return tr("Audio active (%1, %2)").arg(audioFormatSummary(), audioStreamSummary());
            }
            return tr("Audio state changed");
        }();

        qInfo().noquote() << QStringLiteral("[audio]") << status;
        switch (state) {
        case QtAudio::IdleState:
            pumpAudio();
            emit statusChanged(status);
            break;
        case QtAudio::SuspendedState:
            emit statusChanged(status);
            break;
        case QtAudio::StoppedState:
            if (sink_ && sink_->error() != QtAudio::NoError) {
                emit errorOccurred(tr("Audio sink stopped with error %1").arg(static_cast<int>(sink_->error())));
            }
            emit statusChanged(status);
            break;
        case QtAudio::ActiveState:
            emit statusChanged(status);
            break;
        }
    });
    sinkDevice_ = sink_->start();
    pendingPcm_.clear();
    if (pumpTimer_) {
        pumpTimer_->start();
    }
    emit statusChanged(tr("Audio connected on %1 (%2)")
                           .arg(audioDeviceSummary(), audioFormatSummary()));
}

void GrpcAudioClient::enqueuePcm(const QByteArray &pcm, quint64 sequence) {
    if (haveLastChunkSequence_ && sequence > lastChunkSequence_ + 1) {
        droppedChunks_ += sequence - lastChunkSequence_ - 1;
    }
    lastChunkSequence_ = sequence;
    haveLastChunkSequence_ = true;
    ++receivedChunks_;

    pendingPcm_.append(pcm);
    const qsizetype maxQueuedBytes = static_cast<qsizetype>(sampleRate_) * outputFormat_.bytesPerFrame() / 2;
    if (pendingPcm_.size() > maxQueuedBytes) {
        pendingPcm_.remove(0, pendingPcm_.size() - maxQueuedBytes);
    }
    pumpAudio();
}

void GrpcAudioClient::pumpAudio() {
    if (!sink_ || !sinkDevice_ || pendingPcm_.isEmpty()) {
        return;
    }

    qint64 bytesFree = sink_->bytesFree();
    if (bytesFree <= 0) {
        return;
    }

    const qint64 frameBytes = std::max(1, outputFormat_.bytesPerFrame());
    bytesFree -= bytesFree % frameBytes;
    if (bytesFree <= 0) {
        return;
    }

    const qint64 targetBufferedBytes = static_cast<qint64>(sampleRate_) * outputFormat_.bytesPerFrame() / 12;
    if (pendingPcm_.size() < targetBufferedBytes && sink_->state() != QtAudio::ActiveState) {
        return;
    }

    const qint64 toWrite = std::min<qint64>(bytesFree, pendingPcm_.size());
    const qint64 alignedToWrite = toWrite - (toWrite % frameBytes);
    if (alignedToWrite <= 0) {
        return;
    }

    const qint64 written = sinkDevice_->write(pendingPcm_.constData(), alignedToWrite);
    if (written > 0) {
        pendingPcm_.remove(0, static_cast<qsizetype>(written));
    }
}

QByteArray GrpcAudioClient::packStereoSample(float left, float right) const {
    left = std::clamp(left, -1.0f, 1.0f);
    right = std::clamp(right, -1.0f, 1.0f);

    QByteArray bytes;
    if (outputFormat_.sampleFormat() == QAudioFormat::Float) {
        bytes.resize(outputFormat_.bytesPerFrame());
        auto *out = reinterpret_cast<float *>(bytes.data());
        out[0] = left;
        out[1] = right;
        return bytes;
    }

    if (outputFormat_.sampleFormat() == QAudioFormat::Int16) {
        bytes.resize(outputFormat_.bytesPerFrame());
        auto *out = reinterpret_cast<qint16 *>(bytes.data());
        out[0] = static_cast<qint16>(std::clamp(static_cast<int>(left * 32767.0f), -32768, 32767));
        out[1] = static_cast<qint16>(std::clamp(static_cast<int>(right * 32767.0f), -32768, 32767));
        return bytes;
    }

    if (outputFormat_.sampleFormat() == QAudioFormat::Int32) {
        bytes.resize(outputFormat_.bytesPerFrame());
        auto *out = reinterpret_cast<qint32 *>(bytes.data());
        out[0] = static_cast<qint32>(std::clamp(static_cast<double>(left) * 2147483647.0, -2147483648.0, 2147483647.0));
        out[1] = static_cast<qint32>(std::clamp(static_cast<double>(right) * 2147483647.0, -2147483648.0, 2147483647.0));
        return bytes;
    }

    if (outputFormat_.sampleFormat() == QAudioFormat::UInt8) {
        bytes.resize(outputFormat_.bytesPerFrame());
        auto *out = reinterpret_cast<unsigned char *>(bytes.data());
        out[0] = static_cast<unsigned char>(std::clamp((left * 127.0f) + 128.0f, 0.0f, 255.0f));
        out[1] = static_cast<unsigned char>(std::clamp((right * 127.0f) + 128.0f, 0.0f, 255.0f));
        return bytes;
    }

    bytes.resize(outputFormat_.bytesPerFrame());
    std::memset(bytes.data(), 0, static_cast<size_t>(bytes.size()));
    return bytes;
}

void GrpcAudioClient::invokeOnOwnerThread(const std::function<void()> &fn, bool blocking) {
    if (QThread::currentThread() == thread()) {
        fn();
        return;
    }

    QMetaObject::invokeMethod(
        this,
        fn,
        blocking ? Qt::BlockingQueuedConnection : Qt::QueuedConnection
    );
}
