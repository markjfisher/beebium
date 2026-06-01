#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

struct RomEntry {
    QString slotLabel;
    int slotNumber = -1;
    bool isRam = false;
    QString content;
};

struct ConfigProfile {
    QString id;
    QString name;
    QString modelId;
    QString modelName;
    QString discInterfaceId;
    QString discInterfaceName;
    QString hostOs;
    QVector<RomEntry> romEntries;
    bool externalMemory = false;
    bool beebLink = false;
    bool videoNuLA = false;
    bool mouse = false;
    bool romBoard = false;
    QString tubeMode = QStringLiteral("none");
    QString tubeOs;
    QString serialMode = QStringLiteral("None");
    QString serialPath;
    int serialBaud = 19200;
};

struct ConfigTemplateOption {
    QString id;
    QString label;
    QString modelId;
    QString modelName;
    QString discInterfaceId;
    QString discInterfaceName;
};

QString configProfilesFilePath();
QString presetsOutputDirPath();
QVector<ConfigTemplateOption> defaultConfigTemplates();
ConfigProfile createProfileFromTemplate(const ConfigTemplateOption &option);
QVector<ConfigProfile> loadConfigProfiles();
bool saveConfigProfiles(const QVector<ConfigProfile> &profiles);
QString writePresetFile(const ConfigProfile &profile);
QJsonObject toJson(const ConfigProfile &profile);
ConfigProfile fromJson(const QJsonObject &json);
