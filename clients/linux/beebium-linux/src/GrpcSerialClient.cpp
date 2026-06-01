#include "GrpcSerialClient.hpp"

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

GrpcSerialClient::GrpcSerialClient(QObject *parent)
    : QObject(parent) {
    pollTimer_.setInterval(500);
    connect(&pollTimer_, &QTimer::timeout, this, &GrpcSerialClient::refresh);
}

void GrpcSerialClient::connectToTarget(const ConnectionTarget &target) {
    disconnectFromTarget();
    target_ = target;
    channel_ = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    stub_ = beebium::SerialService::NewStub(channel_);
    refresh();
    pollTimer_.start();
}

void GrpcSerialClient::disconnectFromTarget() {
    pollTimer_.stop();
    status_ = {};
    stub_.reset();
    channel_.reset();
    emit statusChanged();
}

const SerialStatusInfo &GrpcSerialClient::status() const {
    return status_;
}

bool GrpcSerialClient::setEndpointMode(beebium::SerialEndpointMode mode, const QString &path, quint32 baud) {
    if (!stub_) {
        return false;
    }

    grpc::ClientContext context;
    beebium::SetEndpointModeRequest request;
    beebium::SetEndpointModeResponse response;
    request.set_mode(mode);
    request.set_path(path.toStdString());
    request.set_baud(baud);
    const grpc::Status rpcStatus = stub_->SetEndpointMode(&context, request, &response);
    if (!rpcStatus.ok()) {
        emit errorOccurred(tr("SetEndpointMode failed: %1").arg(QString::fromStdString(rpcStatus.error_message())));
        return false;
    }
    if (!response.success()) {
        emit errorOccurred(tr("SetEndpointMode rejected: %1").arg(QString::fromStdString(response.error())));
        return false;
    }
    refresh();
    return true;
}

void GrpcSerialClient::refresh() {
    if (!stub_) {
        return;
    }

    grpc::ClientContext context;
    beebium::GetSerialStatusRequest request;
    beebium::SerialStatus response;
    const grpc::Status rpcStatus = stub_->GetSerialStatus(&context, request, &response);
    if (!rpcStatus.ok()) {
        emit errorOccurred(tr("GetSerialStatus failed: %1").arg(QString::fromStdString(rpcStatus.error_message())));
        return;
    }

    status_.hasSerialSocket = response.has_serial_socket();
    status_.aciaControl = response.acia_control();
    status_.aciaStatus = response.acia_status();
    status_.tdre = response.tdre();
    status_.rdrf = response.rdrf();
    status_.notDcd = response.not_dcd();
    status_.notCts = response.not_cts();
    status_.irqPending = response.irq_pending();
    status_.ulaControl = response.ula_control();
    status_.txBaud = response.tx_baud();
    status_.rxBaud = response.rx_baud();
    status_.rs423Selected = response.rs423_selected();
    status_.motorOn = response.motor_on();
    status_.txPending = response.tx_pending();
    status_.rxPending = response.rx_pending();
    status_.endpointMode = static_cast<int>(response.endpoint_mode());
    status_.endpointPath = QString::fromStdString(response.endpoint_path());
    status_.endpointOpen = response.endpoint_open();
    emit statusChanged();
}
