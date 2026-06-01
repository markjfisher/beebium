#pragma once

#include <memory>

#include <QObject>
#include <QTimer>
#include <QVector>

#include <grpcpp/channel.h>

#include "ClientTypes.hpp"
#include "ConnectionTarget.hpp"
#include "disc.grpc.pb.h"

class GrpcDiscClient final : public QObject {
    Q_OBJECT

public:
    explicit GrpcDiscClient(QObject *parent = nullptr);

    void connectToTarget(const ConnectionTarget &target);
    void disconnectFromTarget();

    [[nodiscard]] const QVector<DiscDriveInfo> &drives() const;
    [[nodiscard]] bool hasDiscController() const;
    [[nodiscard]] QString controllerType() const;

    bool insertDisc(int drive, const QString &localFilePath, bool writeProtectOverride = false);
    bool ejectDisc(int drive, bool immediate = false);

public slots:
    void refresh();

signals:
    void statusChanged();
    void errorOccurred(const QString &message);

private:
    ConnectionTarget target_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::DiscService::Stub> stub_;
    QTimer pollTimer_;
    QVector<DiscDriveInfo> drives_;
    bool hasDiscController_ = false;
    QString controllerType_;
};
