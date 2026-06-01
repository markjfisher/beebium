#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

struct IndicatorInfo {
    QString name;
    QString label;
    QString color;
    QString relatedKey;
    quint32 value = 0;
};

struct DiscDriveInfo {
    int drive = 0;
    QString state;
    QString discName;
    QString discUrl;
    QString format;
    bool motorOn = false;
    bool writeProtected = false;
    int currentTrack = 0;
};

struct SidewaysSocketInfo {
    int socketIndex = 0;
    QString socketLabel;
    QVector<int> slotNumbers;
    QString type;
    bool populated = false;
    QString imageName;
    QString title;
    QString version;
    QStringList kinds;
    bool supportsRom = false;
    bool supportsRam = false;
    bool supportsEmpty = false;
    bool runtimeConfigurable = false;
};

struct SerialStatusInfo {
    bool hasSerialSocket = false;
    quint32 aciaControl = 0;
    quint32 aciaStatus = 0;
    bool tdre = false;
    bool rdrf = false;
    bool notDcd = false;
    bool notCts = false;
    bool irqPending = false;
    quint32 ulaControl = 0;
    quint32 txBaud = 0;
    quint32 rxBaud = 0;
    bool rs423Selected = false;
    bool motorOn = false;
    quint32 txPending = 0;
    quint32 rxPending = 0;
    int endpointMode = 0;
    QString endpointPath;
    bool endpointOpen = false;
};

Q_DECLARE_METATYPE(IndicatorInfo)
Q_DECLARE_METATYPE(DiscDriveInfo)
Q_DECLARE_METATYPE(SidewaysSocketInfo)
Q_DECLARE_METATYPE(SerialStatusInfo)
