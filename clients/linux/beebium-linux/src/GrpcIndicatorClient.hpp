#pragma once

#include <memory>

#include <QObject>
#include <QTimer>
#include <QVector>

#include <grpcpp/channel.h>

#include "ClientTypes.hpp"
#include "ConnectionTarget.hpp"
#include "indicator.grpc.pb.h"

class GrpcIndicatorClient final : public QObject {
    Q_OBJECT

public:
    explicit GrpcIndicatorClient(QObject *parent = nullptr);

    void connectToTarget(const ConnectionTarget &target);
    void disconnectFromTarget();

    [[nodiscard]] const QVector<IndicatorInfo> &indicators() const;

public slots:
    void refresh();

signals:
    void indicatorsChanged();
    void errorOccurred(const QString &message);

private:
    void fetchMetadata();

    ConnectionTarget target_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::IndicatorService::Stub> stub_;
    QTimer pollTimer_;
    QVector<IndicatorInfo> indicators_;
    quint64 sequence_ = 0;
    bool metadataLoaded_ = false;
};
