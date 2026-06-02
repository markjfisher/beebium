#pragma once

#include <QObject>

#include "ConfigProfiles.hpp"

class QProcess;

class LocalServerManager final : public QObject {
    Q_OBJECT

public:
    explicit LocalServerManager(QObject *parent = nullptr);
    ~LocalServerManager() override;

    bool applyConfig(const ConfigProfile &profile);
    void stop();
    void setKeepRunningOnExit(bool enabled);
    void prepareForAppExit();
    [[nodiscard]] bool ownsRunningServer() const;

signals:
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);
    void configApplied(const QString &profileName);
    void serverExited(const QString &message);
    void ownershipChanged(bool owned);

private:
    void startPendingLaunch();
    QString resolveExecutablePath(const QString &modelId) const;
    QString detachedLogFilePath() const;

    QProcess *process_ = nullptr;
    QString pendingExecutablePath_;
    QString pendingPresetPath_;
    QString pendingProfileName_;
    QString runningExecutablePath_;
    QStringList runningArguments_;
    bool suppressNextExit_ = false;
    bool launchInProgress_ = false;
    bool keepRunningOnExit_ = false;
};
