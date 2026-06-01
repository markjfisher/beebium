#pragma once

#include <memory>

#include <QObject>
#include <QTimer>
#include <QVector>

#include <grpcpp/channel.h>

#include "ClientTypes.hpp"
#include "ConnectionTarget.hpp"
#include "sideways.grpc.pb.h"

class GrpcSidewaysClient final : public QObject {
    Q_OBJECT

public:
    explicit GrpcSidewaysClient(QObject *parent = nullptr);

    void connectToTarget(const ConnectionTarget &target);
    void disconnectFromTarget();

    [[nodiscard]] const QVector<SidewaysSocketInfo> &sockets() const;
    [[nodiscard]] bool hasAliasing() const;

    bool configureRom(int slot, const QString &localFilePath);
    bool configureRam(int slot);
    bool configureEmpty(int slot);

public slots:
    void refresh();

signals:
    void statusChanged();
    void errorOccurred(const QString &message);

private:
    bool configureSlot(int slot, beebium::SidewaysSlotType type, const QString &localFilePath);

    ConnectionTarget target_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> stub_;
    QTimer pollTimer_;
    QVector<SidewaysSocketInfo> sockets_;
    bool hasAliasing_ = false;
};
