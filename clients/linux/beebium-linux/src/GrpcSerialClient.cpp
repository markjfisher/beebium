#include "GrpcSerialClient.hpp"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

GrpcSerialClient::GrpcSerialClient(QObject *parent)
    : QObject(parent) {
}

void GrpcSerialClient::connectToTarget(const ConnectionTarget &target) {
    disconnectFromTarget();
    target_ = target;
    running_.store(true);
    worker_ = std::thread([this, target] { streamLoop(target); });
}

void GrpcSerialClient::disconnectFromTarget() {
    running_.store(false);
    if (streamContext_) {
        streamContext_->TryCancel();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    streamContext_.reset();
    status_ = {};
    channel_.reset();
    emit statusChanged();
}

const SerialStatusInfo &GrpcSerialClient::status() const {
    return status_;
}

void GrpcSerialClient::streamLoop(ConnectionTarget target) {
    channel_ = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    auto stub = beebium::SerialService::NewStub(channel_);

    streamContext_ = std::make_unique<grpc::ClientContext>();
    beebium::WatchSerialStatusRequest request;
    request.set_min_interval_ms(100);
    std::unique_ptr<grpc::ClientReader<beebium::SerialStatus>> reader(
        stub->WatchSerialStatus(streamContext_.get(), request));

    beebium::SerialStatus response;
    while (running_.load() && reader->Read(&response)) {
        status_.hasSerialSocket = response.has_serial_socket();
        status_.connector = QString::fromStdString(response.connector());
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
        status_.txBitPeriod = response.tx_bit_period();
        status_.rxBitPeriod = response.rx_bit_period();
        emit statusChanged();
    }

    const grpc::Status status = reader->Finish();
    if (running_.load() && !status.ok()) {
        emit errorOccurred(tr("WatchSerialStatus failed: %1").arg(QString::fromStdString(status.error_message())));
    }
}
