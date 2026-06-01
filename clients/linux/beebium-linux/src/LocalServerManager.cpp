#include "LocalServerManager.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace {

QString executableNameForModel(const QString &modelId) {
    if (modelId == QStringLiteral("model-b")) {
        return QStringLiteral("beebium-model-b");
    }
    if (modelId == QStringLiteral("model-b-plus")) {
        return QStringLiteral("beebium-model-b-plus");
    }
    if (modelId == QStringLiteral("model-b-plus-128k")) {
        return QStringLiteral("beebium-model-b-plus-128k");
    }
    if (modelId == QStringLiteral("model-b-romram")) {
        return QStringLiteral("beebium-model-b-romram");
    }
    return QString();
}

} // namespace

LocalServerManager::LocalServerManager(QObject *parent)
    : QObject(parent)
    , process_(new QProcess(this)) {
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::readyReadStandardOutput, this, [this]() {
        const QByteArray output = process_->readAllStandardOutput();
        if (!output.isEmpty()) {
            emit statusChanged(QString::fromLocal8Bit(output).trimmed());
        }
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit errorOccurred(tr("Local server process error: %1").arg(process_->errorString()));
    });
    connect(process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (suppressNextExit_) {
            suppressNextExit_ = false;
            return;
        }
        const QString message = tr("Local server exited (%1, code %2)")
                                    .arg(exitStatus == QProcess::NormalExit ? tr("normal") : tr("crash"))
                                    .arg(exitCode);
        qInfo().noquote() << QStringLiteral("[local-server]") << message;
        emit serverExited(message);
    });
}

LocalServerManager::~LocalServerManager() {
    stop();
}

bool LocalServerManager::applyConfig(const ConfigProfile &profile) {
    const QString presetPath = writePresetFile(profile);
    if (presetPath.isEmpty()) {
        emit errorOccurred(tr("Failed to write preset for '%1'.").arg(profile.name));
        return false;
    }

    const QString executablePath = resolveExecutablePath(profile.modelId);
    if (executablePath.isEmpty()) {
        emit errorOccurred(tr("No local Beebium server executable found for model '%1'.").arg(profile.modelName));
        return false;
    }

    qInfo().noquote() << QStringLiteral("[local-server] model=") << profile.modelId;
    qInfo().noquote() << QStringLiteral("[local-server] executable=") << executablePath;
    qInfo().noquote() << QStringLiteral("[local-server] preset=") << presetPath;
    qInfo().noquote() << QStringLiteral("[local-server] argv=") << (executablePath + QStringLiteral(" --preset ") + presetPath);
    qInfo().noquote() << QStringLiteral("[local-server] cwd=") << QDir::currentPath();

    pendingExecutablePath_ = executablePath;
    pendingPresetPath_ = presetPath;
    pendingProfileName_ = profile.name;
    launchInProgress_ = true;

    if (process_->state() != QProcess::NotRunning) {
        suppressNextExit_ = true;
        stop();
        QTimer::singleShot(500, this, &LocalServerManager::startPendingLaunch);
        return true;
    }

    startPendingLaunch();
    return true;
}

void LocalServerManager::stop() {
    if (process_->state() != QProcess::NotRunning) {
        process_->terminate();
        if (!process_->waitForFinished(3000)) {
            process_->kill();
            process_->waitForFinished(1000);
        }
    }
}

bool LocalServerManager::ownsRunningServer() const {
    return process_->state() != QProcess::NotRunning;
}

void LocalServerManager::startPendingLaunch() {
    if (!launchInProgress_ || pendingExecutablePath_.isEmpty() || pendingPresetPath_.isEmpty()) {
        return;
    }

    process_->setProgram(pendingExecutablePath_);
    process_->setArguments({QStringLiteral("--preset"), pendingPresetPath_});
    process_->start();
    if (!process_->waitForStarted(3000)) {
        emit errorOccurred(tr("Failed to start local server '%1': %2").arg(pendingExecutablePath_, process_->errorString()));
        pendingExecutablePath_.clear();
        pendingPresetPath_.clear();
        pendingProfileName_.clear();
        launchInProgress_ = false;
        return;
    }

    emit statusChanged(tr("Started local server for '%1'").arg(pendingProfileName_));
    emit configApplied(pendingProfileName_);
    pendingExecutablePath_.clear();
    pendingPresetPath_.clear();
    pendingProfileName_.clear();
    launchInProgress_ = false;
}

QString LocalServerManager::resolveExecutablePath(const QString &modelId) const {
    const QString executableName = executableNameForModel(modelId);
    if (executableName.isEmpty()) {
        return QString();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../../../src/server/") + executableName),
        QDir(appDir).filePath(QStringLiteral("../../src/server/") + executableName),
        QDir(QDir::currentPath()).filePath(QStringLiteral("build/src/server/") + executableName),
        QDir(QDir::currentPath()).filePath(QStringLiteral("build-release/src/server/") + executableName),
        QDir(QDir::currentPath()).filePath(QStringLiteral("build-relwithdebinfo/src/server/") + executableName),
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}
