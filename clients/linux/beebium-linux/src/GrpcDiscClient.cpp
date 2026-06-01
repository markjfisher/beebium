#include "GrpcDiscClient.hpp"

#include <QUrl>

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

namespace {

QString driveStateToString(beebium::DiscDriveState state) {
    switch (state) {
    case beebium::DISC_DRIVE_STATE_LOADED:
        return QObject::tr("Loaded");
    case beebium::DISC_DRIVE_STATE_EJECTING:
        return QObject::tr("Ejecting");
    case beebium::DISC_DRIVE_STATE_EMPTY:
    default:
        return QObject::tr("Empty");
    }
}

} // namespace

GrpcDiscClient::GrpcDiscClient(QObject *parent)
    : QObject(parent) {
    pollTimer_.setInterval(1000);
    connect(&pollTimer_, &QTimer::timeout, this, &GrpcDiscClient::refresh);
}

void GrpcDiscClient::connectToTarget(const ConnectionTarget &target) {
    disconnectFromTarget();
    target_ = target;
    channel_ = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    stub_ = beebium::DiscService::NewStub(channel_);
    refresh();
    pollTimer_.start();
}

void GrpcDiscClient::disconnectFromTarget() {
    pollTimer_.stop();
    stub_.reset();
    channel_.reset();
    drives_.clear();
    hasDiscController_ = false;
    controllerType_.clear();
    emit statusChanged();
}

const QVector<DiscDriveInfo> &GrpcDiscClient::drives() const {
    return drives_;
}

bool GrpcDiscClient::hasDiscController() const {
    return hasDiscController_;
}

QString GrpcDiscClient::controllerType() const {
    return controllerType_;
}

bool GrpcDiscClient::insertDisc(int drive, const QString &localFilePath, bool writeProtectOverride) {
    if (!stub_) {
        return false;
    }

    grpc::ClientContext context;
    beebium::InsertDiscRequest request;
    beebium::InsertDiscResponse response;
    request.set_drive(static_cast<quint32>(drive));
    request.set_url(QUrl::fromLocalFile(localFilePath).toString().toStdString());
    request.set_write_protect_override(writeProtectOverride);
    const grpc::Status status = stub_->InsertDisc(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("InsertDisc failed: %1").arg(QString::fromStdString(status.error_message())));
        return false;
    }
    if (!response.success()) {
        emit errorOccurred(tr("InsertDisc rejected: %1").arg(QString::fromStdString(response.error())));
        return false;
    }
    refresh();
    return true;
}

bool GrpcDiscClient::ejectDisc(int drive, bool immediate) {
    if (!stub_) {
        return false;
    }

    grpc::ClientContext context;
    beebium::EjectDiscRequest request;
    beebium::EjectDiscResponse response;
    request.set_drive(static_cast<quint32>(drive));
    request.set_immediate(immediate);
    const grpc::Status status = stub_->EjectDisc(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("EjectDisc failed: %1").arg(QString::fromStdString(status.error_message())));
        return false;
    }
    if (!response.accepted()) {
        emit errorOccurred(tr("EjectDisc rejected: %1").arg(QString::fromStdString(response.error())));
        return false;
    }
    refresh();
    return true;
}

void GrpcDiscClient::refresh() {
    if (!stub_) {
        return;
    }

    grpc::ClientContext context;
    beebium::GetDriveStatusRequest request;
    beebium::GetDriveStatusResponse response;
    const grpc::Status status = stub_->GetDriveStatus(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("GetDriveStatus failed: %1").arg(QString::fromStdString(status.error_message())));
        return;
    }

    hasDiscController_ = response.has_disc_controller();
    controllerType_ = QString::fromStdString(response.controller_type());
    drives_.clear();
    drives_.reserve(response.drives_size());
    for (const auto &drive : response.drives()) {
        DiscDriveInfo info;
        info.drive = static_cast<int>(drive.drive());
        info.state = driveStateToString(drive.state());
        info.discName = QString::fromStdString(drive.disc_name());
        info.discUrl = QString::fromStdString(drive.disc_url());
        info.format = drive.has_disc() ? QString::fromStdString(drive.disc().format()) : QString();
        info.motorOn = drive.motor_on();
        info.writeProtected = drive.write_protected();
        info.currentTrack = static_cast<int>(drive.current_track());
        drives_.push_back(info);
    }
    emit statusChanged();
}
