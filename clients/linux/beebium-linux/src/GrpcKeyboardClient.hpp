#pragma once

#include <memory>

#include <optional>

#include <QObject>
#include <QHash>
#include <QString>

#include <grpcpp/channel.h>

#include "ConnectionTarget.hpp"
#include "keyboard.grpc.pb.h"

class GrpcKeyboardClient final : public QObject {
    Q_OBJECT

public:
    struct CharacterMapping {
        quint32 ikNumber = 0;
        bool needsShift = false;
        QString name;
    };

    explicit GrpcKeyboardClient(QObject *parent = nullptr);

    void connectToTarget(const ConnectionTarget &target);
    void disconnectFromTarget();

    bool keyDown(quint32 ikNumber);
    bool keyUp(quint32 ikNumber);
    bool breakDown();
    bool breakUp();
    bool typeText(const QString &text);
    std::optional<CharacterMapping> getCharacterMapping(const QString &text);

signals:
    void errorOccurred(const QString &message);

private:
    void clearMappings();

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::KeyboardService::Stub> stub_;
    QHash<QString, CharacterMapping> characterMappings_;
};
