#include "ConfigsDialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

struct KnownRomChoice {
    QString label;
    QString image;
};

std::unique_ptr<QSettings> dialogSettings() {
    const QString configFolder = qApp->property("configFolder").toString();
    if (!configFolder.isEmpty()) {
        return std::make_unique<QSettings>(QDir(configFolder).filePath(QStringLiteral("beebium-linux.ini")), QSettings::IniFormat);
    }
    return std::make_unique<QSettings>();
}

QVector<KnownRomChoice> knownSidewaysRoms() {
    return {
        {QObject::tr("BASIC II"), QStringLiteral("bbc-basic_2.rom")},
        {QObject::tr("Acorn 1770 DFS"), QStringLiteral("acorn-dfs_2_26.rom")},
    };
}

QVector<KnownRomChoice> knownOsRoms() {
    return {
        {QObject::tr("OS 1.20"), QStringLiteral("acorn-mos_1_20.rom")},
    };
}

QString displayRomValue(const QString &value) {
    if (value == QStringLiteral("bbc-basic_2.rom")) {
        return QObject::tr("BASIC II");
    }
    if (value == QStringLiteral("acorn-dfs_2_26.rom")) {
        return QObject::tr("Acorn 1770 DFS");
    }
    if (value == QStringLiteral("acorn-mos_1_20.rom")) {
        return QObject::tr("OS 1.20");
    }
    return value;
}

QString canonicalRomValue(const QString &value) {
    if (value == QObject::tr("BASIC II")) {
        return QStringLiteral("bbc-basic_2.rom");
    }
    if (value == QObject::tr("Acorn 1770 DFS")) {
        return QStringLiteral("acorn-dfs_2_26.rom");
    }
    if (value == QObject::tr("OS 1.20")) {
        return QStringLiteral("acorn-mos_1_20.rom");
    }
    return value;
}

} // namespace

ConfigsDialog::ConfigsDialog(const QVector<ConfigProfile> &profiles, int currentIndex, QWidget *parent)
    : QDialog(parent)
    , profiles_(profiles) {
    setWindowTitle(tr("Edit Configs"));
    resize(1100, 780);
    setModal(false);

    auto *rootLayout = new QVBoxLayout(this);
    auto *splitter = new QSplitter(this);
    rootLayout->addWidget(splitter);

    auto *leftPanel = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    auto *buttonsRow = new QWidget(leftPanel);
    auto *buttonsLayout = new QHBoxLayout(buttonsRow);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    auto *newButton = new QPushButton(tr("New..."), buttonsRow);
    auto *duplicateButton = new QPushButton(tr("Duplicate"), buttonsRow);
    auto *deleteButton = new QPushButton(tr("Delete"), buttonsRow);
    auto *upButton = new QPushButton(tr("Up"), buttonsRow);
    auto *downButton = new QPushButton(tr("Down"), buttonsRow);
    buttonsLayout->addWidget(newButton);
    buttonsLayout->addWidget(duplicateButton);
    buttonsLayout->addWidget(deleteButton);
    buttonsLayout->addWidget(upButton);
    buttonsLayout->addWidget(downButton);
    leftLayout->addWidget(buttonsRow);
    profileList_ = new QListWidget(leftPanel);
    leftLayout->addWidget(profileList_);

    auto *rightPanel = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);

    auto *metaBox = new QGroupBox(tr("Configuration"), rightPanel);
    auto *metaLayout = new QFormLayout(metaBox);
    modelLabel_ = new QLabel(metaBox);
    discLabel_ = new QLabel(metaBox);
    nameEdit_ = new QLineEdit(metaBox);
    hostOsEdit_ = new QLineEdit(metaBox);
    hostOsEdit_->setReadOnly(true);
    hostOsButton_ = new QPushButton(tr("..."), metaBox);
    auto *hostOsRow = new QWidget(metaBox);
    auto *hostOsLayout = new QHBoxLayout(hostOsRow);
    hostOsLayout->setContentsMargins(0, 0, 0, 0);
    hostOsLayout->addWidget(hostOsEdit_);
    hostOsLayout->addWidget(hostOsButton_);
    metaLayout->addRow(tr("Model"), modelLabel_);
    metaLayout->addRow(tr("Disc interface"), discLabel_);
    metaLayout->addRow(tr("Name"), nameEdit_);
    metaLayout->addRow(tr("Host OS"), hostOsRow);
    rightLayout->addWidget(metaBox);

    auto *romBox = new QGroupBox(tr("ROMs / Sideways"), rightPanel);
    auto *romLayout = new QVBoxLayout(romBox);
    romTable_ = new QTableWidget(romBox);
    romTable_->setColumnCount(3);
    romTable_->setHorizontalHeaderLabels({tr("ROM"), tr("RAM"), tr("Contents")});
    romTable_->verticalHeader()->setVisible(false);
    romTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    romTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    romLayout->addWidget(romTable_);
    auto *romButtons = new QWidget(romBox);
    auto *romButtonsLayout = new QHBoxLayout(romButtons);
    romButtonsLayout->setContentsMargins(0, 0, 0, 0);
    auto *chooseRomButton = new QPushButton(tr("..."), romButtons);
    auto *clearRomButton = new QPushButton(tr("Clear"), romButtons);
    romButtonsLayout->addWidget(chooseRomButton);
    romButtonsLayout->addWidget(clearRomButton);
    romButtonsLayout->addStretch();
    romLayout->addWidget(romButtons);
    rightLayout->addWidget(romBox, 1);

    auto *hardwareBox = new QGroupBox(tr("Additional Hardware"), rightPanel);
    auto *hardwareLayout = new QVBoxLayout(hardwareBox);
    externalMemoryCheck_ = new QCheckBox(tr("External memory"), hardwareBox);
    beebLinkCheck_ = new QCheckBox(tr("BeebLink"), hardwareBox);
    videoNuLACheck_ = new QCheckBox(tr("Video NuLA"), hardwareBox);
    mouseCheck_ = new QCheckBox(tr("Mouse"), hardwareBox);
    romBoardCheck_ = new QCheckBox(tr("ROM board"), hardwareBox);
    hardwareLayout->addWidget(externalMemoryCheck_);
    hardwareLayout->addWidget(beebLinkCheck_);
    hardwareLayout->addWidget(videoNuLACheck_);
    hardwareLayout->addWidget(mouseCheck_);
    hardwareLayout->addWidget(romBoardCheck_);
    rightLayout->addWidget(hardwareBox);

    auto *tubeBox = new QGroupBox(tr("Tube"), rightPanel);
    auto *tubeLayout = new QFormLayout(tubeBox);
    tubeNoneRadio_ = new QRadioButton(tr("No second processor"), tubeBox);
    tube6502Radio_ = new QRadioButton(tr("6502 second processor"), tubeBox);
    tubeTurboRadio_ = new QRadioButton(tr("Master Turbo"), tubeBox);
    tubeOsEdit_ = new QLineEdit(tubeBox);
    tubeLayout->addRow(tubeNoneRadio_);
    tubeLayout->addRow(tube6502Radio_);
    tubeLayout->addRow(tubeTurboRadio_);
    tubeLayout->addRow(tr("OS"), tubeOsEdit_);
    rightLayout->addWidget(tubeBox);

    auto *serialBox = new QGroupBox(tr("Serial Defaults"), rightPanel);
    auto *serialLayout = new QFormLayout(serialBox);
    serialModeCombo_ = new QComboBox(serialBox);
    serialModeCombo_->addItems({tr("None"), tr("Loopback"), tr("Scriptable"), tr("PTY"), tr("Device")});
    serialBaudCombo_ = new QComboBox(serialBox);
    for (int baud : {0, 75, 110, 150, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200}) {
        serialBaudCombo_->addItem(baud == 0 ? tr("Default (19200)") : QString::number(baud), baud);
    }
    serialPathEdit_ = new QLineEdit(serialBox);
    serialLayout->addRow(tr("Mode"), serialModeCombo_);
    serialLayout->addRow(tr("Path"), serialPathEdit_);
    serialLayout->addRow(tr("Baud"), serialBaudCombo_);
    rightLayout->addWidget(serialBox);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    rootLayout->addWidget(buttonBox);

    connect(newButton, &QPushButton::clicked, this, &ConfigsDialog::newProfile);
    connect(duplicateButton, &QPushButton::clicked, this, &ConfigsDialog::duplicateProfile);
    connect(deleteButton, &QPushButton::clicked, this, &ConfigsDialog::deleteProfile);
    connect(upButton, &QPushButton::clicked, this, &ConfigsDialog::moveProfileUp);
    connect(downButton, &QPushButton::clicked, this, &ConfigsDialog::moveProfileDown);
    connect(profileList_, &QListWidget::currentRowChanged, this, &ConfigsDialog::profileSelectionChanged);
    connect(chooseRomButton, &QPushButton::clicked, this, &ConfigsDialog::chooseRomContent);
    connect(hostOsButton_, &QPushButton::clicked, this, &ConfigsDialog::chooseHostOs);
    connect(clearRomButton, &QPushButton::clicked, this, [this]() {
        const int row = romTable_->currentRow();
        if (row >= 0 && currentRow() >= 0 && row < profiles_[currentRow()].romEntries.size()) {
            profiles_[currentRow()].romEntries[row].content.clear();
            loadProfileIntoEditor(currentRow());
        }
    });
    connect(buttonBox->button(QDialogButtonBox::Save), &QPushButton::clicked, this, &ConfigsDialog::saveAndClose);
    connect(buttonBox->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &QDialog::close);

    rebuildList();
    profileList_->setCurrentRow(currentIndex >= 0 && currentIndex < profiles_.size() ? currentIndex : 0);
}

void ConfigsDialog::newProfile() {
    QStringList items;
    const auto templates = defaultConfigTemplates();
    for (const auto &option : templates) {
        items << option.label;
    }
    bool ok = false;
    const QString selected = QInputDialog::getItem(this, tr("New Config"), tr("Template"), items, 0, false, &ok);
    if (!ok || selected.isEmpty()) {
        return;
    }
    for (const auto &option : templates) {
        if (option.label == selected) {
            profiles_.push_back(createProfileFromTemplate(option));
            rebuildList();
            profileList_->setCurrentRow(profiles_.size() - 1);
            return;
        }
    }
}

void ConfigsDialog::duplicateProfile() {
    const int row = currentRow();
    if (row < 0) {
        return;
    }
    storeEditorIntoCurrentProfile();
    ConfigProfile copy = profiles_.at(row);
    copy.name += tr(" Copy");
    copy.id += QStringLiteral("-copy");
    profiles_.insert(row + 1, copy);
    rebuildList();
    profileList_->setCurrentRow(row + 1);
}

void ConfigsDialog::deleteProfile() {
    const int row = currentRow();
    if (row < 0) {
        return;
    }
    profiles_.removeAt(row);
    if (profiles_.isEmpty()) {
        profiles_.push_back(createProfileFromTemplate(defaultConfigTemplates().at(0)));
    }
    rebuildList();
    profileList_->setCurrentRow(std::min(row, static_cast<int>(profiles_.size()) - 1));
}

void ConfigsDialog::moveProfileUp() {
    const int row = currentRow();
    if (row <= 0) {
        return;
    }
    storeEditorIntoCurrentProfile();
    profiles_.swapItemsAt(row, row - 1);
    rebuildList();
    profileList_->setCurrentRow(row - 1);
}

void ConfigsDialog::moveProfileDown() {
    const int row = currentRow();
    if (row < 0 || row >= profiles_.size() - 1) {
        return;
    }
    storeEditorIntoCurrentProfile();
    profiles_.swapItemsAt(row, row + 1);
    rebuildList();
    profileList_->setCurrentRow(row + 1);
}

void ConfigsDialog::profileSelectionChanged() {
    loadProfileIntoEditor(currentRow());
}

void ConfigsDialog::chooseRomContent() {
    const int profileIndex = currentRow();
    const int row = romTable_->currentRow();
    if (profileIndex < 0 || row < 0 || row >= profiles_[profileIndex].romEntries.size()) {
        return;
    }

    QMenu menu(this);
    QAction *fileAction = menu.addAction(tr("File..."));
    QMenu *recentMenu = menu.addMenu(tr("Recent file"));
    const QStringList recents = recentRomPaths();
    if (recents.isEmpty()) {
        recentMenu->setEnabled(false);
    } else {
        for (const QString &path : recents) {
            QAction *recentAction = recentMenu->addAction(path);
            connect(recentAction, &QAction::triggered, this, [this, profileIndex, row, path]() {
                profiles_[profileIndex].romEntries[row].content = path;
                addRecentRomPath(path);
                loadProfileIntoEditor(profileIndex);
            });
        }
    }
    menu.addSeparator();
    QAction *emptyAction = menu.addAction(tr("(empty)"));
    if (profiles_[profileIndex].romEntries[row].slotNumber >= 0) {
        QMenu *knownMenu = menu.addMenu(tr("B Sideways ROM"));
        for (const auto &choice : knownSidewaysRoms()) {
            QAction *choiceAction = knownMenu->addAction(choice.label);
            connect(choiceAction, &QAction::triggered, this, [this, profileIndex, row, choice]() {
                profiles_[profileIndex].romEntries[row].content = choice.image;
                loadProfileIntoEditor(profileIndex);
            });
        }
    }

    QAction *selected = menu.exec(QCursor::pos());
    if (selected == fileAction) {
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose ROM image"));
        if (!path.isEmpty()) {
            profiles_[profileIndex].romEntries[row].content = path;
            addRecentRomPath(path);
            loadProfileIntoEditor(profileIndex);
        }
    } else if (selected == emptyAction) {
        profiles_[profileIndex].romEntries[row].content.clear();
        loadProfileIntoEditor(profileIndex);
    }
}

void ConfigsDialog::chooseHostOs() {
    const int profileIndex = currentRow();
    if (profileIndex < 0) {
        return;
    }

    QMenu menu(this);
    QAction *fileAction = menu.addAction(tr("File..."));
    QMenu *recentMenu = menu.addMenu(tr("Recent file"));
    const QStringList recents = recentRomPaths();
    if (recents.isEmpty()) {
        recentMenu->setEnabled(false);
    } else {
        for (const QString &path : recents) {
            QAction *recentAction = recentMenu->addAction(path);
            connect(recentAction, &QAction::triggered, this, [this, profileIndex, path]() {
                profiles_[profileIndex].hostOs = path;
                addRecentRomPath(path);
                loadProfileIntoEditor(profileIndex);
            });
        }
    }
    menu.addSeparator();
    QMenu *knownMenu = menu.addMenu(tr("B OS ROM"));
    for (const auto &choice : knownOsRoms()) {
        QAction *choiceAction = knownMenu->addAction(choice.label);
        connect(choiceAction, &QAction::triggered, this, [this, profileIndex, choice]() {
            profiles_[profileIndex].hostOs = choice.image;
            loadProfileIntoEditor(profileIndex);
        });
    }

    QAction *selected = menu.exec(QCursor::pos());
    if (selected == fileAction) {
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose OS ROM"));
        if (!path.isEmpty()) {
            profiles_[profileIndex].hostOs = path;
            addRecentRomPath(path);
            loadProfileIntoEditor(profileIndex);
        }
    }
}

void ConfigsDialog::saveAndClose() {
    storeEditorIntoCurrentProfile();
    if (!saveConfigProfiles(profiles_)) {
        QMessageBox::warning(this, tr("Edit Configs"), tr("Failed to save config profiles."));
        return;
    }
    emit configsSaved(profiles_, currentRow());
}

void ConfigsDialog::rebuildList() {
    profileList_->clear();
    for (const auto &profile : profiles_) {
        profileList_->addItem(profile.name);
    }
}

void ConfigsDialog::loadProfileIntoEditor(int index) {
    if (index < 0 || index >= profiles_.size()) {
        return;
    }
    loading_ = true;
    const ConfigProfile &profile = profiles_.at(index);
    modelLabel_->setText(profile.modelName);
    discLabel_->setText(profile.discInterfaceName);
    nameEdit_->setText(profile.name);
    hostOsEdit_->setText(displayRomValue(profile.hostOs));
    romTable_->setRowCount(profile.romEntries.size());
    for (int row = 0; row < profile.romEntries.size(); ++row) {
        const auto &entry = profile.romEntries.at(row);
        romTable_->setItem(row, 0, new QTableWidgetItem(entry.slotLabel));
        auto *ramItem = new QTableWidgetItem();
        ramItem->setCheckState(entry.isRam ? Qt::Checked : Qt::Unchecked);
        romTable_->setItem(row, 1, ramItem);
        romTable_->setItem(row, 2, new QTableWidgetItem(displayRomValue(entry.content)));
    }
    externalMemoryCheck_->setChecked(profile.externalMemory);
    beebLinkCheck_->setChecked(profile.beebLink);
    videoNuLACheck_->setChecked(profile.videoNuLA);
    mouseCheck_->setChecked(profile.mouse);
    romBoardCheck_->setChecked(profile.romBoard);
    tubeNoneRadio_->setChecked(profile.tubeMode == QStringLiteral("none"));
    tube6502Radio_->setChecked(profile.tubeMode == QStringLiteral("6502"));
    tubeTurboRadio_->setChecked(profile.tubeMode == QStringLiteral("turbo"));
    tubeOsEdit_->setText(profile.tubeOs);
    serialModeCombo_->setCurrentText(profile.serialMode);
    serialPathEdit_->setText(profile.serialPath);
    const int baudIndex = serialBaudCombo_->findData(profile.serialBaud);
    serialBaudCombo_->setCurrentIndex(baudIndex >= 0 ? baudIndex : serialBaudCombo_->findData(19200));
    loading_ = false;
}

void ConfigsDialog::storeEditorIntoCurrentProfile() {
    const int index = currentRow();
    if (loading_ || index < 0 || index >= profiles_.size()) {
        return;
    }
    ConfigProfile &profile = profiles_[index];
    profile.name = nameEdit_->text().trimmed();
    profile.hostOs = canonicalRomValue(hostOsEdit_->text().trimmed());
    for (int row = 0; row < profile.romEntries.size() && row < romTable_->rowCount(); ++row) {
        profile.romEntries[row].isRam = romTable_->item(row, 1) && romTable_->item(row, 1)->checkState() == Qt::Checked;
        profile.romEntries[row].content = romTable_->item(row, 2)
            ? canonicalRomValue(romTable_->item(row, 2)->text().trimmed())
            : QString();
    }
    profile.externalMemory = externalMemoryCheck_->isChecked();
    profile.beebLink = beebLinkCheck_->isChecked();
    profile.videoNuLA = videoNuLACheck_->isChecked();
    profile.mouse = mouseCheck_->isChecked();
    profile.romBoard = romBoardCheck_->isChecked();
    profile.tubeMode = tube6502Radio_->isChecked() ? QStringLiteral("6502") : (tubeTurboRadio_->isChecked() ? QStringLiteral("turbo") : QStringLiteral("none"));
    profile.tubeOs = tubeOsEdit_->text().trimmed();
    profile.serialMode = serialModeCombo_->currentText();
    profile.serialPath = serialPathEdit_->text().trimmed();
    profile.serialBaud = serialBaudCombo_->currentData().toInt();
    if (profile.name.isEmpty()) {
        profile.name = profile.modelName;
    }
}

int ConfigsDialog::currentRow() const {
    return profileList_->currentRow();
}

void ConfigsDialog::addRecentRomPath(const QString &path) {
    if (path.isEmpty()) {
        return;
    }
    auto settings = dialogSettings();
    QStringList recents = settings->value(QStringLiteral("linuxUi/recentRomPaths")).toStringList();
    recents.removeAll(path);
    recents.prepend(path);
    while (recents.size() > 8) {
        recents.removeLast();
    }
    settings->setValue(QStringLiteral("linuxUi/recentRomPaths"), recents);
}

QStringList ConfigsDialog::recentRomPaths() const {
    auto settings = dialogSettings();
    return settings->value(QStringLiteral("linuxUi/recentRomPaths")).toStringList();
}
