#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include <QObject>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>

#include "ClientTypes.hpp"
#include "ConnectionTarget.hpp"
#include "serial.grpc.pb.h"

class GrpcSerialClient final : public QObject {
    Q_OBJECT

public:
    explicit GrpcSerialClient(QObject *parent = nullptr);

    void connectToTarget(const ConnectionTarget &target);
    void disconnectFromTarget();

    [[nodiscard]] const SerialStatusInfo &status() const;

signals:
    void statusChanged();
    void errorOccurred(const QString &message);

private:
    void streamLoop(ConnectionTarget target);

    ConnectionTarget target_;
    std::atomic_bool running_{false};
    std::thread worker_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<grpc::ClientContext> streamContext_;
    SerialStatusInfo status_;
};
