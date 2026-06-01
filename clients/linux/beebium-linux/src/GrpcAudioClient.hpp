#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include <QAudioFormat>
#include <QIODevice>
#include <QObject>
#include <QStringList>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>

#include "ConnectionTarget.hpp"
#include "audio.grpc.pb.h"

class QAudioSink;
class QMutex;
class QTimer;

class AudioBufferDevice final : public QIODevice {
    Q_OBJECT

public:
    explicit AudioBufferDevice(QObject *parent = nullptr);

    void start();
    void stop();
    void resetBuffer();
    void pushPcm(const QByteArray &pcm);

protected:
    [[nodiscard]] bool isSequential() const override;
    [[nodiscard]] qint64 bytesAvailable() const override;
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private:
    QByteArray buffer_;
    qint64 maxBytes_ = 48000 * 4;
    std::unique_ptr<QMutex> mutex_;
};

class GrpcAudioClient final : public QObject {
    Q_OBJECT

public:
    explicit GrpcAudioClient(QObject *parent = nullptr);
    ~GrpcAudioClient() override;

    void connectToTarget(const ConnectionTarget &target);
    void disconnectFromTarget();

    [[nodiscard]] QString audioFormatSummary() const;
    [[nodiscard]] QString audioDeviceSummary() const;
    [[nodiscard]] QString audioStreamSummary() const;
    [[nodiscard]] QStringList availableOutputDevices() const;
    [[nodiscard]] QString preferredOutputDeviceDescription() const;
    void setPreferredOutputDeviceDescription(const QString &description);
    void setVolume(float volume);
    [[nodiscard]] float volume() const;

signals:
    void errorOccurred(const QString &message);
    void statusChanged(const QString &status);

private:
    void invokeOnOwnerThread(const std::function<void()> &fn, bool blocking);
    void streamLoop(ConnectionTarget target);
    QByteArray decodeChunk(const beebium::AudioChunk &chunk) const;
    void configureAudioSink(quint32 sampleRate);
    QByteArray packStereoSample(float left, float right) const;
    void enqueuePcm(const QByteArray &pcm, quint64 sequence);
    void pumpAudio();

    std::atomic_bool running_{false};
    std::thread worker_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<grpc::ClientContext> streamContext_;
    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<AudioBufferDevice> device_;
    QIODevice *sinkDevice_ = nullptr;
    std::unique_ptr<QTimer> pumpTimer_;
    QAudioFormat outputFormat_;
    QString outputDeviceDescription_;
    QString preferredOutputDeviceDescription_;
    float volume_ = 0.35f;
    QByteArray pendingPcm_;
    quint64 receivedChunks_ = 0;
    quint64 droppedChunks_ = 0;
    quint64 lastChunkSequence_ = 0;
    bool haveLastChunkSequence_ = false;
    quint32 sampleRate_ = 48000;
    quint32 sourceCount_ = 4;
    beebium::SourceEncoding sourceEncoding_ = beebium::ENCODING_4X8BIT_UNSIGNED;
};
