#include "GrpcVideoClient.hpp"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

GrpcVideoClient::GrpcVideoClient(QObject *parent)
    : QObject(parent) {
}

GrpcVideoClient::~GrpcVideoClient() {
    disconnectFromTarget();
}

void GrpcVideoClient::connectToTarget(const ConnectionTarget &target) {
    disconnectFromTarget();

    running_.store(true);
    emit connectionStateChanged(tr("Connecting to %1").arg(target.address()));
    worker_ = std::thread([this, target] { streamLoop(target); });
}

void GrpcVideoClient::disconnectFromTarget() {
    running_.store(false);
    if (streamContext_) {
        streamContext_->TryCancel();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    streamContext_.reset();
    channel_.reset();
}

void GrpcVideoClient::streamLoop(ConnectionTarget target) {
    channel_ = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    auto stub = beebium::VideoService::NewStub(channel_);

    grpc::ClientContext configContext;
    beebium::GetConfigRequest configRequest;
    beebium::VideoConfig configResponse;
    grpc::Status configStatus = stub->GetConfig(&configContext, configRequest, &configResponse);
    if (!configStatus.ok()) {
        emit errorOccurred(tr("GetConfig failed: %1").arg(QString::fromStdString(configStatus.error_message())));
        emit connectionStateChanged(tr("Disconnected"));
        return;
    }

    emit connectionStateChanged(tr("Connected to %1").arg(target.address()));

    streamContext_ = std::make_unique<grpc::ClientContext>();
    beebium::SubscribeFramesRequest request;
    std::unique_ptr<grpc::ClientReader<beebium::Frame>> reader(
        stub->SubscribeFrames(streamContext_.get(), request));

    beebium::Frame frame;
    while (running_.load() && reader->Read(&frame)) {
        FrameGeometry geometry;
        geometry.width = static_cast<int>(frame.width());
        geometry.height = static_cast<int>(frame.height());
        geometry.displayWidth = static_cast<int>(frame.display_width());
        geometry.displayHeight = static_cast<int>(frame.display_height());
        geometry.leftBorder = static_cast<int>(frame.left_border());
        geometry.rightBorder = static_cast<int>(frame.right_border());
        geometry.topBorder = static_cast<int>(frame.top_border());
        geometry.bottomBorder = static_cast<int>(frame.bottom_border());
        geometry.interlaced = frame.field_order() != beebium::PROGRESSIVE;

        emit frameReady(QByteArray(frame.pixels().data(), static_cast<int>(frame.pixels().size())),
                        geometry,
                        static_cast<quint64>(frame.frame_number()));
    }

    const grpc::Status status = reader->Finish();
    if (running_.load() && !status.ok()) {
        emit errorOccurred(tr("Frame stream ended: %1").arg(QString::fromStdString(status.error_message())));
    }
    emit connectionStateChanged(tr("Disconnected"));
}
