#include "GrpcSidewaysClient.hpp"

#include <QUrl>

#include <algorithm>

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

namespace {

QString sidewaysTypeToString(beebium::SidewaysSlotType type) {
    switch (type) {
    case beebium::SIDEWAYS_SLOT_TYPE_ROM:
        return QObject::tr("ROM");
    case beebium::SIDEWAYS_SLOT_TYPE_RAM:
        return QObject::tr("RAM");
    case beebium::SIDEWAYS_SLOT_TYPE_EMPTY:
    default:
        return QObject::tr("Empty");
    }
}

} // namespace

GrpcSidewaysClient::GrpcSidewaysClient(QObject *parent)
    : QObject(parent) {
    pollTimer_.setInterval(2000);
    connect(&pollTimer_, &QTimer::timeout, this, &GrpcSidewaysClient::refresh);
}

void GrpcSidewaysClient::connectToTarget(const ConnectionTarget &target) {
    disconnectFromTarget();
    target_ = target;
    channel_ = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    stub_ = beebium::SidewaysService::NewStub(channel_);
    refresh();
    pollTimer_.start();
}

void GrpcSidewaysClient::disconnectFromTarget() {
    pollTimer_.stop();
    sockets_.clear();
    hasAliasing_ = false;
    stub_.reset();
    channel_.reset();
    emit statusChanged();
}

const QVector<SidewaysSocketInfo> &GrpcSidewaysClient::sockets() const {
    return sockets_;
}

bool GrpcSidewaysClient::hasAliasing() const {
    return hasAliasing_;
}

bool GrpcSidewaysClient::configureRom(int slot, const QString &localFilePath) {
    return configureSlot(slot, beebium::SIDEWAYS_SLOT_TYPE_ROM, localFilePath);
}

bool GrpcSidewaysClient::configureRam(int slot) {
    return configureSlot(slot, beebium::SIDEWAYS_SLOT_TYPE_RAM, QString());
}

bool GrpcSidewaysClient::configureEmpty(int slot) {
    return configureSlot(slot, beebium::SIDEWAYS_SLOT_TYPE_EMPTY, QString());
}

bool GrpcSidewaysClient::configureSlot(int slot, beebium::SidewaysSlotType type, const QString &localFilePath) {
    if (!stub_) {
        return false;
    }

    grpc::ClientContext context;
    beebium::ConfigureSlotRequest request;
    beebium::ConfigureSlotResponse response;
    request.set_slot(static_cast<quint32>(slot));
    request.set_type(type);
    if (!localFilePath.isEmpty()) {
        request.set_url(QUrl::fromLocalFile(localFilePath).toString().toStdString());
    }
    const grpc::Status status = stub_->ConfigureSlot(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("ConfigureSlot failed: %1").arg(QString::fromStdString(status.error_message())));
        return false;
    }
    if (!response.success()) {
        emit errorOccurred(tr("ConfigureSlot rejected: %1").arg(QString::fromStdString(response.error())));
        return false;
    }
    refresh();
    return true;
}

void GrpcSidewaysClient::refresh() {
    if (!stub_) {
        return;
    }

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;
    const grpc::Status status = stub_->GetSlotStatus(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("GetSlotStatus failed: %1").arg(QString::fromStdString(status.error_message())));
        return;
    }

    hasAliasing_ = response.has_aliasing();
    sockets_.clear();
    sockets_.reserve(response.sockets_size());
    for (const auto &socket : response.sockets()) {
        SidewaysSocketInfo info;
        info.socketIndex = static_cast<int>(socket.socket_index());
        info.socketLabel = QString::fromStdString(socket.socket_label());
        for (auto slot : socket.aliased_slots()) {
            info.slotNumbers.push_back(static_cast<int>(slot));
        }
        info.type = sidewaysTypeToString(socket.type());
        info.populated = socket.populated();
        info.imageName = QString::fromStdString(socket.image_name());
        if (socket.has_capabilities()) {
            info.supportsRom = socket.capabilities().supports_rom();
            info.supportsRam = socket.capabilities().supports_ram();
            info.supportsEmpty = socket.capabilities().supports_empty();
            info.runtimeConfigurable = socket.capabilities().runtime_configurable();
        }
        if (socket.has_rom_header() && socket.rom_header().recognised()) {
            info.title = QString::fromStdString(socket.rom_header().title());
            info.version = QString::fromStdString(socket.rom_header().version());
            for (const auto &kind : socket.rom_header().kinds()) {
                info.kinds.push_back(QString::fromStdString(kind));
            }
        }
        sockets_.push_back(info);
    }
    std::sort(sockets_.begin(), sockets_.end(), [](const SidewaysSocketInfo &lhs, const SidewaysSocketInfo &rhs) {
        const int lhsPriority = lhs.slotNumbers.isEmpty() ? lhs.socketIndex : *std::max_element(lhs.slotNumbers.begin(), lhs.slotNumbers.end());
        const int rhsPriority = rhs.slotNumbers.isEmpty() ? rhs.socketIndex : *std::max_element(rhs.slotNumbers.begin(), rhs.slotNumbers.end());
        return lhsPriority > rhsPriority;
    });
    emit statusChanged();
}
