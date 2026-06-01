#pragma once

#include <memory>

#include <QObject>
#include <QTimer>

#include <grpcpp/channel.h>

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
    bool setEndpointMode(beebium::SerialEndpointMode mode, const QString &path, quint32 baud);

public slots:
    void refresh();

signals:
    void statusChanged();
    void errorOccurred(const QString &message);

private:
    ConnectionTarget target_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SerialService::Stub> stub_;
    QTimer pollTimer_;
    SerialStatusInfo status_;
};
