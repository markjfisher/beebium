#pragma once

#include <memory>

#include <QObject>

#include "ConnectionTarget.hpp"

namespace grpc {
class Channel;
}

namespace beebium {
class SystemService;
}

class GrpcSystemClient final : public QObject {
    Q_OBJECT

public:
    explicit GrpcSystemClient(QObject *parent = nullptr);
    void fetchSystemInfo(const ConnectionTarget &target);

signals:
    void machineSummaryChanged(const QString &summary);
    void errorOccurred(const QString &message);
};
