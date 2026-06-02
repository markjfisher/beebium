#include "ConfigProfiles.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QMap>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString defaultProfilesDir() {
    const QString configFolder = qApp->property("configFolder").toString();
    if (!configFolder.isEmpty()) {
        return configFolder;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString slugify(QString value) {
    value = value.toLower();
    for (QChar &ch : value) {
        if (!ch.isLetterOrNumber()) {
            ch = QLatin1Char('-');
        }
    }
    while (value.contains(QStringLiteral("--"))) {
        value.replace(QStringLiteral("--"), QStringLiteral("-"));
    }
    return value.trimmed().remove(QRegularExpression(QStringLiteral("^-|-$")));
}

QVector<RomEntry> defaultRomEntries(bool compactModelB) {
    QVector<RomEntry> entries;
    entries.push_back({QStringLiteral("Host OS"), -1, false, QStringLiteral("OS 1.20")});
    const QStringList labels = compactModelB
        ? QStringList{QStringLiteral("F"), QStringLiteral("E"), QStringLiteral("D"), QStringLiteral("C")}
        : QStringList{QStringLiteral("F"), QStringLiteral("E"), QStringLiteral("D"), QStringLiteral("C"),
                      QStringLiteral("B"), QStringLiteral("A"), QStringLiteral("9"), QStringLiteral("8"),
                      QStringLiteral("7"), QStringLiteral("6"), QStringLiteral("5"), QStringLiteral("4"),
                      QStringLiteral("3"), QStringLiteral("2"), QStringLiteral("1"), QStringLiteral("0")};
    for (int i = 0; i < labels.size(); ++i) {
        entries.push_back({labels.at(i), 15 - i, false, QString()});
    }
    return entries;
}

void normalizeRomEntriesForProfile(ConfigProfile &profile) {
    const bool compactModelB = profile.modelId == QStringLiteral("model-b") && !profile.romBoard;
    if (!compactModelB) {
        if (profile.romEntries.isEmpty()) {
            profile.romEntries = defaultRomEntries(false);
        }
        return;
    }

    // Migrate legacy 16-slot Model B profiles into the real four physical sockets
    // (F/E/D/C). Old profiles could place ROMs into aliased logical slots like B/A,
    // which cannot coexist with BASIC in F on actual hardware.
    if (profile.romEntries.size() > 5) {
        QVector<RomEntry> compactEntries = defaultRomEntries(true);

        auto assignContent = [&](int slot, const QString &content, bool isRam) {
            for (RomEntry &entry : compactEntries) {
                if (entry.slotNumber == slot) {
                    entry.content = content;
                    entry.isRam = isRam;
                    return;
                }
            }
        };

        QString basicContent;
        QString dfsContent;
        QVector<RomEntry> extras;
        for (const RomEntry &entry : profile.romEntries) {
            if (entry.slotNumber < 0 || entry.content.trimmed().isEmpty()) {
                continue;
            }
            const QString canonicalContent = entry.content.trimmed();
            if (canonicalContent == QStringLiteral("BASIC II") || canonicalContent == QStringLiteral("bbc-basic_2.rom")) {
                if (basicContent.isEmpty()) {
                    basicContent = canonicalContent;
                }
                continue;
            }
            if (canonicalContent == QStringLiteral("Acorn DFS 2.26") || canonicalContent == QStringLiteral("acorn-dfs_2_26.rom")) {
                if (dfsContent.isEmpty()) {
                    dfsContent = canonicalContent;
                }
                continue;
            }
            bool duplicate = false;
            for (const RomEntry &existing : extras) {
                if (existing.content == canonicalContent) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                extras.push_back(entry);
            }
        }

        assignContent(15, basicContent.isEmpty() ? QStringLiteral("BASIC II") : basicContent, false);
        QVector<int> preferredSlots;
        if (!dfsContent.isEmpty()) {
            assignContent(14, dfsContent, false);
            preferredSlots = {13, 12};
        } else {
            preferredSlots = {14, 13, 12};
        }
        for (int i = 0; i < extras.size() && i < preferredSlots.size(); ++i) {
            assignContent(preferredSlots.at(i), extras.at(i).content, extras.at(i).isRam);
        }

        profile.romEntries = compactEntries;
        return;
    }

    QMap<int, RomEntry> canonicalEntries;
    for (const RomEntry &entry : profile.romEntries) {
        if (entry.slotNumber < 0) {
            continue;
        }
        const int canonicalSlot = 12 + (entry.slotNumber % 4);
        const bool isBetterMatch = !canonicalEntries.contains(canonicalSlot)
            || entry.slotNumber == canonicalSlot;
        if (isBetterMatch) {
            RomEntry normalized = entry;
            normalized.slotNumber = canonicalSlot;
            normalized.slotLabel = QString(QChar('F' - (15 - canonicalSlot)));
            canonicalEntries[canonicalSlot] = normalized;
        }
    }

    QVector<RomEntry> compactEntries = defaultRomEntries(true);
    for (RomEntry &entry : compactEntries) {
        if (entry.slotNumber >= 0 && canonicalEntries.contains(entry.slotNumber)) {
            entry.content = canonicalEntries[entry.slotNumber].content;
            entry.isRam = canonicalEntries[entry.slotNumber].isRam;
        }
    }
    profile.romEntries = compactEntries;
}

void applyModelDefaults(ConfigProfile &profile) {
    profile.romEntries = defaultRomEntries(profile.modelId == QStringLiteral("model-b") && !profile.romBoard);
    auto setSlot = [&](int slot, const QString &content) {
        for (RomEntry &entry : profile.romEntries) {
            if (entry.slotNumber == slot) {
                entry.content = content;
                break;
            }
        }
    };

    profile.hostOs = QStringLiteral("OS 1.20");
    setSlot(15, QStringLiteral("BASIC II"));
    if (profile.discInterfaceId == QStringLiteral("acorn-1770")) {
        setSlot(14, QStringLiteral("Acorn DFS 2.26"));
    }
    if (profile.modelId == QStringLiteral("model-b-plus") || profile.modelId == QStringLiteral("model-b-plus-128k")) {
        profile.hostOs = QStringLiteral("B+ MOS");
    }
    if (profile.modelId == QStringLiteral("model-b-romram")) {
        profile.romBoard = true;
    }
    normalizeRomEntriesForProfile(profile);
}

} // namespace

QString configProfilesFilePath() {
    const QString dir = defaultProfilesDir();
    QDir().mkpath(dir);
    return QFileInfo(QDir(dir).filePath(QStringLiteral("beebium-linux-configs.json"))).absoluteFilePath();
}

QString presetsOutputDirPath() {
    const QString dir = QDir(defaultProfilesDir()).filePath(QStringLiteral("presets"));
    QDir().mkpath(dir);
    return QFileInfo(dir).absoluteFilePath();
}

QVector<ConfigTemplateOption> defaultConfigTemplates() {
    return {
        {QStringLiteral("model-b"), QStringLiteral("BBC Model B"), QStringLiteral("model-b"), QStringLiteral("BBC Model B"), QStringLiteral("none"), QStringLiteral("None")},
        {QStringLiteral("model-b-acorn-1770"), QStringLiteral("BBC Model B / Acorn 1770"), QStringLiteral("model-b"), QStringLiteral("BBC Model B"), QStringLiteral("acorn-1770"), QStringLiteral("Acorn 1770")},
        {QStringLiteral("model-b-plus"), QStringLiteral("BBC Model B+ 64K"), QStringLiteral("model-b-plus"), QStringLiteral("BBC Model B+ 64K"), QStringLiteral("built-in-wd1770"), QStringLiteral("Built-in WD1770")},
        {QStringLiteral("model-b-plus-128k"), QStringLiteral("BBC Model B+ 128K"), QStringLiteral("model-b-plus-128k"), QStringLiteral("BBC Model B+ 128K"), QStringLiteral("built-in-wd1770"), QStringLiteral("Built-in WD1770")},
        {QStringLiteral("model-b-romram"), QStringLiteral("BBC Model B with ROM/RAM Board"), QStringLiteral("model-b-romram"), QStringLiteral("BBC Model B with ROM/RAM Board"), QStringLiteral("none"), QStringLiteral("None")},
    };
}

ConfigProfile createProfileFromTemplate(const ConfigTemplateOption &option) {
    ConfigProfile profile;
    profile.id = slugify(option.label);
    profile.name = option.label;
    profile.modelId = option.modelId;
    profile.modelName = option.modelName;
    profile.discInterfaceId = option.discInterfaceId;
    profile.discInterfaceName = option.discInterfaceName;
    applyModelDefaults(profile);
    return profile;
}

QJsonObject toJson(const ConfigProfile &profile) {
    QJsonObject json;
    json[QStringLiteral("id")] = profile.id;
    json[QStringLiteral("name")] = profile.name;
    json[QStringLiteral("modelId")] = profile.modelId;
    json[QStringLiteral("modelName")] = profile.modelName;
    json[QStringLiteral("discInterfaceId")] = profile.discInterfaceId;
    json[QStringLiteral("discInterfaceName")] = profile.discInterfaceName;
    json[QStringLiteral("hostOs")] = profile.hostOs;
    QJsonArray romEntries;
    for (const RomEntry &entry : profile.romEntries) {
        QJsonObject item;
        item[QStringLiteral("slotLabel")] = entry.slotLabel;
        item[QStringLiteral("slotNumber")] = entry.slotNumber;
        item[QStringLiteral("isRam")] = entry.isRam;
        item[QStringLiteral("content")] = entry.content;
        romEntries.push_back(item);
    }
    json[QStringLiteral("romEntries")] = romEntries;
    json[QStringLiteral("externalMemory")] = profile.externalMemory;
    json[QStringLiteral("beebLink")] = profile.beebLink;
    json[QStringLiteral("videoNuLA")] = profile.videoNuLA;
    json[QStringLiteral("mouse")] = profile.mouse;
    json[QStringLiteral("romBoard")] = profile.romBoard;
    json[QStringLiteral("tubeMode")] = profile.tubeMode;
    json[QStringLiteral("tubeOs")] = profile.tubeOs;
    json[QStringLiteral("serialMode")] = profile.serialMode;
    json[QStringLiteral("serialPath")] = profile.serialPath;
    json[QStringLiteral("serialBaud")] = profile.serialBaud;
    return json;
}

ConfigProfile fromJson(const QJsonObject &json) {
    ConfigProfile profile;
    profile.id = json.value(QStringLiteral("id")).toString();
    profile.name = json.value(QStringLiteral("name")).toString();
    profile.modelId = json.value(QStringLiteral("modelId")).toString();
    profile.modelName = json.value(QStringLiteral("modelName")).toString();
    profile.discInterfaceId = json.value(QStringLiteral("discInterfaceId")).toString();
    profile.discInterfaceName = json.value(QStringLiteral("discInterfaceName")).toString();
    profile.hostOs = json.value(QStringLiteral("hostOs")).toString();
    const QJsonArray romEntries = json.value(QStringLiteral("romEntries")).toArray();
    for (const QJsonValue &value : romEntries) {
        const QJsonObject item = value.toObject();
        profile.romEntries.push_back({item.value(QStringLiteral("slotLabel")).toString(),
                                      item.value(QStringLiteral("slotNumber")).toInt(-1),
                                      item.value(QStringLiteral("isRam")).toBool(false),
                                      item.value(QStringLiteral("content")).toString()});
    }
    profile.externalMemory = json.value(QStringLiteral("externalMemory")).toBool(false);
    profile.beebLink = json.value(QStringLiteral("beebLink")).toBool(false);
    profile.videoNuLA = json.value(QStringLiteral("videoNuLA")).toBool(false);
    profile.mouse = json.value(QStringLiteral("mouse")).toBool(false);
    profile.romBoard = json.value(QStringLiteral("romBoard")).toBool(false);
    profile.tubeMode = json.value(QStringLiteral("tubeMode")).toString(QStringLiteral("none"));
    profile.tubeOs = json.value(QStringLiteral("tubeOs")).toString();
    profile.serialMode = json.value(QStringLiteral("serialMode")).toString(QStringLiteral("None"));
    profile.serialPath = json.value(QStringLiteral("serialPath")).toString();
    profile.serialBaud = json.value(QStringLiteral("serialBaud")).toInt(19200);
    if (profile.romEntries.isEmpty()) {
        applyModelDefaults(profile);
    } else {
        normalizeRomEntriesForProfile(profile);
    }
    return profile;
}

QVector<ConfigProfile> loadConfigProfiles() {
    QFile file(configProfilesFilePath());
    if (!file.exists()) {
        QVector<ConfigProfile> defaults;
        defaults.push_back(createProfileFromTemplate(defaultConfigTemplates().at(1)));
        return defaults;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    QVector<ConfigProfile> profiles;
    for (const QJsonValue &value : document.array()) {
        profiles.push_back(fromJson(value.toObject()));
    }
    return profiles;
}

bool saveConfigProfiles(const QVector<ConfigProfile> &profiles) {
    QFile file(configProfilesFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QJsonArray array;
    for (const ConfigProfile &profile : profiles) {
        array.push_back(toJson(profile));
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return true;
}

QString writePresetFile(const ConfigProfile &profile) {
    auto mapKnownRom = [](const QString &value) {
        if (value == QStringLiteral("BASIC II")) {
            return QStringLiteral("bbc-basic_2.rom");
        }
        if (value == QStringLiteral("Acorn DFS 2.26")) {
            return QStringLiteral("acorn-dfs_2_26.rom");
        }
        return value;
    };

    auto canonicalSidewaysSlot = [&](int slotNumber) {
        if (profile.modelId == QStringLiteral("model-b")) {
            return 12 + (slotNumber % 4);
        }
        return slotNumber;
    };

    QJsonObject root;
    root[QStringLiteral("name")] = profile.name;
    root[QStringLiteral("model")] = profile.modelId;

    QJsonObject storage;
    QJsonObject fdcSocket;
    fdcSocket[QStringLiteral("id")] = profile.discInterfaceId == QStringLiteral("none")
        ? QStringLiteral("none")
        : profile.discInterfaceId;
    storage[QStringLiteral("fdc_socket")] = fdcSocket;
    root[QStringLiteral("storage")] = storage;

    QMap<int, RomEntry> groupedEntries;
    for (const RomEntry &entry : profile.romEntries) {
        if (entry.slotNumber < 0) {
            continue;
        }
        const int canonicalSlot = canonicalSidewaysSlot(entry.slotNumber);
        auto existing = groupedEntries.find(canonicalSlot);
        if (existing == groupedEntries.end() || entry.slotNumber > existing->slotNumber) {
            RomEntry normalized = entry;
            normalized.slotNumber = canonicalSlot;
            groupedEntries[canonicalSlot] = normalized;
        }
    }

    QJsonArray slotArray;
    for (const RomEntry &entry : groupedEntries) {
        QJsonObject slot;
        slot[QStringLiteral("slot")] = entry.slotNumber;
        const QString content = entry.content.trimmed();
        if (entry.isRam) {
            slot[QStringLiteral("type")] = QStringLiteral("ram");
            if (!content.isEmpty()) {
                slot[QStringLiteral("image_uri")] = mapKnownRom(content);
            }
        } else if (content.isEmpty()) {
            slot[QStringLiteral("type")] = QStringLiteral("empty");
        } else {
            slot[QStringLiteral("type")] = QStringLiteral("rom");
            slot[QStringLiteral("image_uri")] = mapKnownRom(content);
        }
        qInfo().noquote() << QStringLiteral("[preset] sideways slot=") << entry.slotNumber
                          << QStringLiteral("type=") << slot.value(QStringLiteral("type")).toString()
                          << QStringLiteral("image_uri=") << slot.value(QStringLiteral("image_uri")).toString();
        slotArray.push_back(slot);
    }
    QJsonObject sideways;
    sideways[QStringLiteral("slots")] = slotArray;
    root[QStringLiteral("sideways_bank")] = sideways;

    const QString presetPath = QFileInfo(QDir(presetsOutputDirPath()).filePath(slugify(profile.name).isEmpty()
                                                                                    ? QStringLiteral("config.preset.beebium")
                                                                                    : slugify(profile.name) + QStringLiteral(".preset.beebium"))).absoluteFilePath();
    QFile file(presetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return presetPath;
}
