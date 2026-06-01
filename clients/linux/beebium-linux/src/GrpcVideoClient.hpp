#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include <QObject>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>

#include "ConnectionTarget.hpp"
#include "FrameGeometry.hpp"
#include "video.grpc.pb.h"

class GrpcVideoClient final : public QObject {
    Q_OBJECT

public:
    explicit GrpcVideoClient(QObject *parent = nullptr);
    ~GrpcVideoClient() override;

    void connectToTarget(const ConnectionTarget &target);
    void disconnectFromTarget();

signals:
    void connectionStateChanged(const QString &state);
    void frameReady(const QByteArray &bgraPixels, const FrameGeometry &geometry, quint64 frameNumber);
    void errorOccurred(const QString &message);

private:
    void streamLoop(ConnectionTarget target);

    std::atomic_bool running_{false};
    std::thread worker_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<grpc::ClientContext> streamContext_;
};
