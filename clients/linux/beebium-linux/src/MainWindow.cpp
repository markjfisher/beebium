#include "MainWindow.hpp"

#include <algorithm>

#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLoggingCategory>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/LayoutSaver.h>
#else
#include <QDockWidget>
#endif

#include "GrpcAudioClient.hpp"
#include "ConfigProfiles.hpp"
#include "ConfigsDialog.hpp"
#include "GrpcDiscClient.hpp"
#include "GrpcIndicatorClient.hpp"
#include "GrpcKeyboardClient.hpp"
#include "LocalServerManager.hpp"
#include "GrpcSerialClient.hpp"
#include "GrpcSidewaysClient.hpp"
#include "GrpcSystemClient.hpp"
#include "GrpcVideoClient.hpp"
#include "VideoWidget.hpp"
#include "system.grpc.pb.h"

namespace {

constexpr int kUiLayoutVersion = 2;
Q_LOGGING_CATEGORY(mainUiLog, "beebium.linux.ui")

std::unique_ptr<QSettings> makeSettings() {
    const QString configFolder = qApp->property("configFolder").toString();
    if (!configFolder.isEmpty()) {
        return std::make_unique<QSettings>(QDir(configFolder).filePath(QStringLiteral("beebium-linux.ini")), QSettings::IniFormat);
    }
    return std::make_unique<QSettings>();
}

QString layoutFilePath() {
    const QString configFolder = qApp->property("configFolder").toString();
    if (configFolder.isEmpty()) {
        return QString();
    }
    return QDir(configFolder).filePath(QStringLiteral("beebium-linux-layout.json"));
}

QString serialModeName(int mode) {
    switch (mode) {
    case beebium::SERIAL_ENDPOINT_NONE:
        return QObject::tr("None");
    case beebium::SERIAL_ENDPOINT_LOOPBACK:
        return QObject::tr("Loopback");
    case beebium::SERIAL_ENDPOINT_SCRIPTABLE:
        return QObject::tr("Scriptable");
    case beebium::SERIAL_ENDPOINT_PTY:
        return QObject::tr("PTY");
    case beebium::SERIAL_ENDPOINT_DEVICE:
        return QObject::tr("Device");
    default:
        return QObject::tr("Unknown");
    }
}

QString audioPrimaryStatus(const QString &message) {
    const int end = message.indexOf(QLatin1Char('('));
    return end > 0 ? message.left(end).trimmed() : message;
}

QString audioParenthetical(const QString &message) {
    const int start = message.indexOf(QLatin1Char('('));
    const int end = message.lastIndexOf(QLatin1Char(')'));
    if (start >= 0 && end > start) {
        return message.mid(start + 1, end - start - 1);
    }
    return QString();
}

} // namespace

MainWindow::MainWindow(const ConnectionTarget &initialTarget, QWidget *parent)
#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
    : KDDockWidgets::QtWidgets::MainWindow(QStringLiteral("BeebiumLinuxMainWindow"), KDDockWidgets::MainWindowOption_HasCentralFrame, parent)
#else
    : QMainWindow(parent)
#endif
    , videoClient_(new GrpcVideoClient(this))
    , keyboardClient_(new GrpcKeyboardClient(this))
    , audioClient_(new GrpcAudioClient(this))
    , systemClient_(new GrpcSystemClient(this))
    , indicatorClient_(new GrpcIndicatorClient(this))
    , discClient_(new GrpcDiscClient(this))
    , sidewaysClient_(new GrpcSidewaysClient(this))
    , serialClient_(new GrpcSerialClient(this))
    , localServerManager_(new LocalServerManager(this)) {
    setWindowTitle(tr("Beebium Linux"));
    resize(1400, 900);

    hostEdit_ = new QLineEdit(initialTarget.host, this);
    portEdit_ = new QLineEdit(QString::number(initialTarget.port), this);
    portEdit_->setValidator(new QIntValidator(1, 65535, portEdit_));
    connectButton_ = new QPushButton(tr("Connect"), this);
    resetButton_ = new QPushButton(tr("Reset"), this);
    machineLabel_ = new QLabel(tr("Not connected"), this);
    statusLabel_ = new QLabel(tr("Idle"), this);
    targetLabel_ = new QLabel(initialTarget.address(), this);
    controllerLabel_ = new QLabel(tr("No controller"), this);
    aliasingLabel_ = new QLabel(tr("Unknown"), this);
    audioStatusLabel_ = new QLabel(tr("Audio idle"), this);
    audioFormatLabel_ = new QLabel(tr("No audio format negotiated"), this);
    audioStreamLabel_ = new QLabel(tr("No stream data yet"), this);
    audioDeviceLabel_ = new QLabel(tr("Default output"), this);
    serialEndpointLabel_ = new QLabel(tr("No endpoint"), this);
    serialStateLabel_ = new QLabel(tr("Not connected"), this);
    serialHostBaudLabel_ = new QLabel(tr("Default (19200)"), this);
    serialQueueLabel_ = new QLabel(tr("TX 0 / RX 0"), this);
    configSummaryLabel_ = new QLabel(this);
    configSummaryLabel_->setWordWrap(true);
    audioDeviceCombo_ = new QComboBox(this);
    audioVolumeSlider_ = new QSlider(Qt::Horizontal, this);
    indicatorTable_ = new QTableWidget(this);
    storageTable_ = new QTableWidget(this);
    sidewaysTable_ = new QTableWidget(this);
    serialModeCombo_ = new QComboBox(this);
    serialPathEdit_ = new QLineEdit(this);
    serialBaudCombo_ = new QComboBox(this);
    videoWidget_ = new VideoWidget(this);
    videoWidget_->setKeyboardClient(keyboardClient_);
    configProfiles_ = loadConfigProfiles();

    buildDockLayout();
    buildMenus();

    statusBar()->showMessage(tr("Ready"));

    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::connectToServer);
    connect(resetButton_, &QPushButton::clicked, this, &MainWindow::resetMachine);
    connect(videoClient_, &GrpcVideoClient::frameReady, videoWidget_, &VideoWidget::presentFrame);
    connect(videoClient_, &GrpcVideoClient::connectionStateChanged, statusLabel_, &QLabel::setText);
    connect(videoClient_, &GrpcVideoClient::connectionStateChanged, this, [this](const QString &message) {
        statusBar()->showMessage(message);
    });
    connect(videoClient_, &GrpcVideoClient::errorOccurred, this, &MainWindow::showError);
    connect(keyboardClient_, &GrpcKeyboardClient::errorOccurred, this, &MainWindow::showError);
    connect(audioClient_, &GrpcAudioClient::errorOccurred, this, &MainWindow::showError);
    connect(audioClient_, &GrpcAudioClient::statusChanged, this, &MainWindow::showAudioStatus);
    connect(systemClient_, &GrpcSystemClient::machineSummaryChanged, this, &MainWindow::updateMachineSummary);
    connect(systemClient_, &GrpcSystemClient::errorOccurred, this, &MainWindow::showError);
    connect(indicatorClient_, &GrpcIndicatorClient::indicatorsChanged, this, &MainWindow::refreshIndicatorsView);
    connect(indicatorClient_, &GrpcIndicatorClient::errorOccurred, this, &MainWindow::showError);
    connect(discClient_, &GrpcDiscClient::statusChanged, this, &MainWindow::refreshStorageView);
    connect(discClient_, &GrpcDiscClient::errorOccurred, this, &MainWindow::showError);
    connect(sidewaysClient_, &GrpcSidewaysClient::statusChanged, this, &MainWindow::refreshSidewaysView);
    connect(sidewaysClient_, &GrpcSidewaysClient::errorOccurred, this, &MainWindow::showError);
    connect(serialClient_, &GrpcSerialClient::statusChanged, this, &MainWindow::refreshSerialView);
    connect(serialClient_, &GrpcSerialClient::errorOccurred, this, &MainWindow::showError);
    connect(localServerManager_, &LocalServerManager::errorOccurred, this, [this](const QString &message) {
        applyingConfig_ = false;
        pendingReconnectAttempts_ = 0;
        showError(message);
    });
    connect(localServerManager_, &LocalServerManager::statusChanged, this, [this](const QString &message) {
        if (!message.isEmpty()) {
            statusBar()->showMessage(message, 5000);
        }
    });
    connect(localServerManager_, &LocalServerManager::serverExited, this, [this](const QString &message) {
        applyingConfig_ = false;
        pendingReconnectAttempts_ = 0;
        disconnectFromServer();
        showError(message);
    });
    connect(localServerManager_, &LocalServerManager::configApplied, this, [this](const QString &) {
        QTimer::singleShot(1000, this, &MainWindow::attemptConfigReconnect);
    });

    connect(serialModeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        if (!serialUiUpdating_) {
            serialControlsDirty_ = true;
        }
    });
    connect(serialPathEdit_, &QLineEdit::textEdited, this, [this]() {
        if (!serialUiUpdating_) {
            serialControlsDirty_ = true;
        }
    });
    connect(serialBaudCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        if (!serialUiUpdating_) {
            serialControlsDirty_ = true;
        }
    });

    restoreUiState();
    syncAudioDeviceChoices();
    audioVolumeSlider_->setValue(static_cast<int>(audioClient_->volume() * 100.0f));
    updateAudioDetails();
    updateConfigSummary();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event) {
    saveUiState();
    disconnectFromServer();
#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
    KDDockWidgets::QtWidgets::MainWindow::closeEvent(event);
#else
    QMainWindow::closeEvent(event);
#endif
}

void MainWindow::buildDockLayout() {
#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
    displayDock_ = new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("display"));
    displayDock_->setTitle(tr("Display"));
    displayDock_->setWidget(videoWidget_);
    docks_.insert(QStringLiteral("display"), displayDock_);
    addDockWidgetAsTab(displayDock_);
#else
    setCentralWidget(videoWidget_);
#endif
    addPanelDock(QStringLiteral("connection"), tr("Connection"), createConnectionPanel(), QStringLiteral("left"));
    addPanelDock(QStringLiteral("status"), tr("Machine Status"), createStatusPanel(), QStringLiteral("left"));
    addPanelDock(QStringLiteral("indicators"), tr("Indicators"), createIndicatorsPanel(), QStringLiteral("right"));
    addPanelDock(QStringLiteral("storage"), tr("Storage"), createStoragePanel(), QStringLiteral("right"));
    addPanelDock(QStringLiteral("config-summary"), tr("Configuration"), createConfigSummaryPanel(), QStringLiteral("right"));
    addPanelDock(QStringLiteral("serial"), tr("Serial"), createSerialPanel(), QStringLiteral("bottom"));
    addPanelDock(QStringLiteral("audio"), tr("Audio"), createAudioPanel(), QStringLiteral("bottom"));
}

void MainWindow::buildMenus() {
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *connectAction = fileMenu->addAction(tr("Connect"));
    connect(connectAction, &QAction::triggered, this, &MainWindow::connectToServer);
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    auto *configsAction = editMenu->addAction(tr("Configs..."));
    connect(configsAction, &QAction::triggered, this, &MainWindow::openConfigsWindow);

    hardwareMenu_ = menuBar()->addMenu(tr("&Hardware"));
    rebuildHardwareMenu();

    auto *emulatorMenu = menuBar()->addMenu(tr("&Emulator"));
    auto *resetAction = emulatorMenu->addAction(tr("Reset / Break"));
    connect(resetAction, &QAction::triggered, this, &MainWindow::resetMachine);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    for (auto it = docks_.cbegin(); it != docks_.cend(); ++it) {
#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
        viewMenu->addAction(it.value()->toggleAction());
#else
        viewMenu->addAction(it.value()->toggleViewAction());
#endif
    }

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *waylandAction = helpMenu->addAction(tr("Wayland Docking Notes"));
    connect(waylandAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(
            this,
            tr("Wayland Docking Notes"),
            tr("On Wayland, floating dock movement and drag-to-redock are more limited than on X11. "
               "KDDockWidgets uses a split model here: the compositor title bar moves a floating window, "
               "while the client title bar is used for dock drag/drop. If a panel is already floating, "
               "use the native title bar to move it and the panel title bar to redock it."));
    });
}

QWidget *MainWindow::createConnectionPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QFormLayout(panel);
    layout->addRow(tr("Host"), hostEdit_);
    layout->addRow(tr("Port"), portEdit_);

    auto *buttonRow = new QWidget(panel);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(connectButton_);
    buttonLayout->addWidget(resetButton_);
    layout->addRow(buttonRow);
    return panel;
}

QWidget *MainWindow::createStatusPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QFormLayout(panel);
    layout->addRow(tr("Target"), targetLabel_);
    layout->addRow(tr("Machine"), machineLabel_);
    layout->addRow(tr("Video"), statusLabel_);
    layout->addRow(tr("Disc controller"), controllerLabel_);
    layout->addRow(tr("Sideways aliasing"), aliasingLabel_);
    return panel;
}

QWidget *MainWindow::createIndicatorsPanel() {
    indicatorTable_->setColumnCount(3);
    indicatorTable_->setHorizontalHeaderLabels({tr("Label"), tr("Value"), tr("Key")});
    indicatorTable_->horizontalHeader()->setStretchLastSection(true);
    indicatorTable_->verticalHeader()->setVisible(false);
    indicatorTable_->setSelectionMode(QAbstractItemView::NoSelection);
    indicatorTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    return indicatorTable_;
}

QWidget *MainWindow::createStoragePanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    storageTable_->setColumnCount(6);
    storageTable_->setHorizontalHeaderLabels({tr("Drive"), tr("State"), tr("Disc"), tr("Motor"), tr("Track"), tr("Write-protect")});
    storageTable_->horizontalHeader()->setStretchLastSection(true);
    storageTable_->verticalHeader()->setVisible(false);
    storageTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    storageTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    storageTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(storageTable_);

    auto *buttonRow = new QWidget(panel);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto *loadButton = new QPushButton(tr("Load Disc"), buttonRow);
    auto *ejectButton = new QPushButton(tr("Eject"), buttonRow);
    auto *refreshButton = new QPushButton(tr("Refresh"), buttonRow);
    buttonLayout->addWidget(loadButton);
    buttonLayout->addWidget(ejectButton);
    buttonLayout->addWidget(refreshButton);
    layout->addWidget(buttonRow);

    connect(loadButton, &QPushButton::clicked, this, [this]() {
        const int drive = selectedDrive();
        if (drive < 0) {
            showError(tr("Select a drive first."));
            return;
        }
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose disc image"));
        if (!path.isEmpty()) {
            discClient_->insertDisc(drive, path);
        }
    });
    connect(ejectButton, &QPushButton::clicked, this, [this]() {
        const int drive = selectedDrive();
        if (drive >= 0) {
            discClient_->ejectDisc(drive);
        }
    });
    connect(refreshButton, &QPushButton::clicked, discClient_, &GrpcDiscClient::refresh);
    return panel;
}

QWidget *MainWindow::createConfigSummaryPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(configSummaryLabel_);
    return panel;
}

QWidget *MainWindow::createSerialPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QFormLayout(panel);
    serialModeCombo_->addItem(tr("None"), beebium::SERIAL_ENDPOINT_NONE);
    serialModeCombo_->addItem(tr("Loopback"), beebium::SERIAL_ENDPOINT_LOOPBACK);
    serialModeCombo_->addItem(tr("Scriptable"), beebium::SERIAL_ENDPOINT_SCRIPTABLE);
    serialModeCombo_->addItem(tr("PTY"), beebium::SERIAL_ENDPOINT_PTY);
    serialModeCombo_->addItem(tr("Device"), beebium::SERIAL_ENDPOINT_DEVICE);

    const QList<int> commonBauds = {0, 75, 110, 150, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
    for (int baud : commonBauds) {
        if (baud == 0) {
            serialBaudCombo_->addItem(tr("Default (19200)"), baud);
        } else {
            serialBaudCombo_->addItem(QString::number(baud), baud);
        }
    }
    serialBaudCombo_->setCurrentIndex(serialBaudCombo_->findData(19200));

    layout->addRow(tr("Mode"), serialModeCombo_);
    layout->addRow(tr("Path"), serialPathEdit_);
    layout->addRow(tr("Host line baud"), serialBaudCombo_);
    layout->addRow(tr("Host requested"), serialHostBaudLabel_);
    layout->addRow(tr("Endpoint"), serialEndpointLabel_);
    layout->addRow(tr("BBC serial"), serialStateLabel_);
    layout->addRow(tr("Queues"), serialQueueLabel_);

    auto *buttons = new QWidget(panel);
    auto *buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto *applyButton = new QPushButton(tr("Apply"), buttons);
    auto *refreshButton = new QPushButton(tr("Refresh"), buttons);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(refreshButton);
    layout->addRow(buttons);

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        const auto mode = static_cast<beebium::SerialEndpointMode>(serialModeCombo_->currentData().toInt());
        const quint32 hostBaud = static_cast<quint32>(serialBaudCombo_->currentData().toUInt());
        if (serialClient_->setEndpointMode(mode, serialPathEdit_->text().trimmed(), hostBaud)) {
            serialControlsDirty_ = false;
            serialHostBaudLabel_->setText(hostBaud == 0 ? tr("Default (19200)") : QString::number(hostBaud));
        }
    });
    connect(refreshButton, &QPushButton::clicked, serialClient_, &GrpcSerialClient::refresh);
    return panel;
}

QWidget *MainWindow::createAudioPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QFormLayout(panel);
    layout->addRow(tr("Status"), audioStatusLabel_);
    layout->addRow(tr("Format"), audioFormatLabel_);
    layout->addRow(tr("Stream"), audioStreamLabel_);
    layout->addRow(tr("Output"), audioDeviceLabel_);
    audioVolumeSlider_->setRange(0, 100);
    layout->addRow(tr("Volume"), audioVolumeSlider_);

    auto *deviceRow = new QWidget(panel);
    auto *deviceLayout = new QHBoxLayout(deviceRow);
    deviceLayout->setContentsMargins(0, 0, 0, 0);
    auto *reconnectButton = new QPushButton(tr("Reconnect Audio"), deviceRow);
    deviceLayout->addWidget(audioDeviceCombo_);
    deviceLayout->addWidget(reconnectButton);
    layout->addRow(tr("Device"), deviceRow);

    connect(audioDeviceCombo_, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        audioClient_->setPreferredOutputDeviceDescription(text);
        saveUiState();
    });
    connect(reconnectButton, &QPushButton::clicked, this, &MainWindow::reconnectAudioStream);
    connect(audioVolumeSlider_, &QSlider::valueChanged, this, [this](int value) {
        audioClient_->setVolume(static_cast<float>(value) / 100.0f);
        saveUiState();
    });
    return panel;
}

void MainWindow::restoreUiState() {
    auto settings = makeSettings();
    hostEdit_->setText(settings->value(QStringLiteral("linuxUi/host"), hostEdit_->text()).toString());
    portEdit_->setText(settings->value(QStringLiteral("linuxUi/port"), portEdit_->text()).toString());
    audioClient_->setPreferredOutputDeviceDescription(settings->value(QStringLiteral("linuxUi/audioDevice")).toString());
    audioClient_->setVolume(settings->value(QStringLiteral("linuxUi/audioVolume"), 0.35).toFloat());
    currentConfigIndex_ = settings->value(QStringLiteral("linuxUi/currentConfigIndex"), 0).toInt();
    updateTargetSummary();
#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
    const QString layoutPath = layoutFilePath();
    if (!layoutPath.isEmpty()) {
        if (QFileInfo::exists(layoutPath)) {
            KDDockWidgets::LayoutSaver saver;
            saver.restoreFromFile(layoutPath);
        }
    } else {
        const int savedVersion = settings->value(QStringLiteral("linuxUi/layoutVersion"), 0).toInt();
        const QByteArray layout = savedVersion == kUiLayoutVersion
            ? settings->value(QStringLiteral("linuxUi/kddwLayout")).toByteArray()
            : QByteArray();
        if (!layout.isEmpty()) {
            KDDockWidgets::LayoutSaver saver;
            saver.restoreLayout(layout);
        }
    }
#else
    restoreGeometry(settings->value(QStringLiteral("linuxUi/geometry")).toByteArray());
    restoreState(settings->value(QStringLiteral("linuxUi/windowState")).toByteArray());
#endif
}

void MainWindow::saveUiState() {
    auto settings = makeSettings();
    settings->setValue(QStringLiteral("linuxUi/host"), hostEdit_->text().trimmed());
    settings->setValue(QStringLiteral("linuxUi/port"), portEdit_->text().trimmed());
    settings->setValue(QStringLiteral("linuxUi/audioDevice"), audioClient_->preferredOutputDeviceDescription());
    settings->setValue(QStringLiteral("linuxUi/audioVolume"), audioClient_->volume());
    settings->setValue(QStringLiteral("linuxUi/currentConfigIndex"), currentConfigIndex_);
    settings->setValue(QStringLiteral("linuxUi/layoutVersion"), kUiLayoutVersion);
#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
    const QString layoutPath = layoutFilePath();
    KDDockWidgets::LayoutSaver saver;
    if (!layoutPath.isEmpty()) {
        saver.saveToFile(layoutPath);
    } else {
        settings->setValue(QStringLiteral("linuxUi/kddwLayout"), saver.serializeLayout());
    }
#else
    settings->setValue(QStringLiteral("linuxUi/geometry"), saveGeometry());
    settings->setValue(QStringLiteral("linuxUi/windowState"), saveState());
#endif
}

void MainWindow::updateTargetSummary() {
    targetLabel_->setText(currentTarget().address());
}

void MainWindow::updateAudioDetails(const QString &message) {
    if (!message.isEmpty()) {
        audioStatusLabel_->setText(audioPrimaryStatus(message));
        const QString details = audioParenthetical(message);
        if (!details.isEmpty()) {
            const QStringList parts = details.split(QStringLiteral(", "));
            if (parts.size() >= 3) {
                audioFormatLabel_->setText(parts.first(3).join(QStringLiteral(", ")));
            }
            if (parts.size() > 3) {
                audioStreamLabel_->setText(parts.sliced(3).join(QStringLiteral(", ")));
            }
        }
    }
    if (audioFormatLabel_->text().isEmpty() || audioFormatLabel_->text() == tr("No audio format negotiated")) {
        audioFormatLabel_->setText(audioClient_->audioFormatSummary());
    }
    if (audioStreamLabel_->text().isEmpty()) {
        audioStreamLabel_->setText(tr("No stream data yet"));
    }
    audioDeviceLabel_->setText(audioClient_->audioDeviceSummary());
}

void MainWindow::updateConfigSummary() {
    if (configProfiles_.isEmpty()) {
        configSummaryLabel_->setText(tr("No saved configs. Use Edit > Configs to create one."));
        return;
    }
    currentConfigIndex_ = std::clamp(currentConfigIndex_, 0, static_cast<int>(configProfiles_.size()) - 1);
    const ConfigProfile &profile = configProfiles_.at(currentConfigIndex_);
    QStringList romLines;
    for (const RomEntry &entry : profile.romEntries) {
        if (!entry.content.isEmpty()) {
            romLines << tr("%1: %2").arg(entry.slotLabel, entry.content);
        }
    }
    const QString serialBaud = profile.serialBaud == 0 ? tr("default baud") : QString::number(profile.serialBaud);
    configSummaryLabel_->setText(
        tr("Name: %1\nModel: %2\nDisc interface: %3\nHost OS: %4\nSerial default: %5, %6, %7\n\nROM contents:\n%8")
            .arg(profile.name,
                 profile.modelName,
                 profile.discInterfaceName,
                 profile.hostOs.isEmpty() ? tr("-") : profile.hostOs,
                 profile.serialMode,
                 profile.serialPath.isEmpty() ? tr("no path") : profile.serialPath,
                 serialBaud,
                 romLines.isEmpty() ? tr("  none configured") : romLines.join(QStringLiteral("\n"))));
}

void MainWindow::rebuildHardwareMenu() {
    if (!hardwareMenu_) {
        return;
    }
    hardwareMenu_->clear();
    auto *configsAction = hardwareMenu_->addAction(tr("Configs..."));
    connect(configsAction, &QAction::triggered, this, &MainWindow::openConfigsWindow);
    hardwareMenu_->addSeparator();

    for (int i = 0; i < configProfiles_.size(); ++i) {
        const ConfigProfile &profile = configProfiles_.at(i);
        QAction *action = hardwareMenu_->addAction(profile.name);
        action->setCheckable(true);
        action->setChecked(i == currentConfigIndex_);
        connect(action, &QAction::triggered, this, [this, i]() {
            selectConfigProfile(i);
        });
    }
}

void MainWindow::openConfigsWindow() {
    if (!configsDialog_) {
        configsDialog_ = new ConfigsDialog(configProfiles_, currentConfigIndex_, this);
        connect(configsDialog_, &ConfigsDialog::configsSaved, this, &MainWindow::handleConfigsSaved);
        connect(configsDialog_, &QDialog::finished, this, [this]() {
            configsDialog_->deleteLater();
            configsDialog_ = nullptr;
        });
    }
    configsDialog_->show();
    configsDialog_->raise();
    configsDialog_->activateWindow();
}

void MainWindow::handleConfigsSaved(const QVector<ConfigProfile> &profiles, int currentIndex) {
    configProfiles_ = profiles;
    currentConfigIndex_ = std::clamp(currentIndex, 0, static_cast<int>(configProfiles_.size()) - 1);
    updateConfigSummary();
    rebuildHardwareMenu();
    saveUiState();
}

void MainWindow::selectConfigProfile(int index) {
    if (configProfiles_.isEmpty()) {
        return;
    }
    currentConfigIndex_ = std::clamp(index, 0, static_cast<int>(configProfiles_.size()) - 1);
    updateConfigSummary();
    rebuildHardwareMenu();
    saveUiState();
    const ConfigProfile &profile = configProfiles_.at(currentConfigIndex_);
    applyingConfig_ = true;
    pendingReconnectAttempts_ = 10;
    disconnectFromServer();
    if (localServerManager_->applyConfig(profile)) {
        statusBar()->showMessage(tr("Applying config '%1' locally...").arg(profile.name), 5000);
    } else {
        applyingConfig_ = false;
        pendingReconnectAttempts_ = 0;
    }
}

void MainWindow::syncAudioDeviceChoices() {
    const QString current = audioClient_->preferredOutputDeviceDescription();
    const QStringList devices = audioClient_->availableOutputDevices();
    audioDeviceCombo_->blockSignals(true);
    audioDeviceCombo_->clear();
    audioDeviceCombo_->addItems(devices);
    if (!current.isEmpty()) {
        const int index = audioDeviceCombo_->findText(current);
        if (index >= 0) {
            audioDeviceCombo_->setCurrentIndex(index);
        }
    }
    audioDeviceCombo_->blockSignals(false);
}

void MainWindow::reconnectAudioStream() {
    audioClient_->disconnectFromTarget();
    audioClient_->setPreferredOutputDeviceDescription(audioDeviceCombo_->currentText());
    saveUiState();
    audioClient_->connectToTarget(currentTarget());
    updateAudioDetails(tr("Audio reconnect requested"));
}

void MainWindow::addPanelDock(const QString &dockId, const QString &title, QWidget *widget, const QString &locationHint) {
#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
    auto *dock = new KDDockWidgets::QtWidgets::DockWidget(dockId);
    dock->setTitle(title);
    dock->setWidget(widget);
    docks_.insert(dockId, dock);

    if (locationHint == QStringLiteral("left")) {
        addDockWidget(dock, KDDockWidgets::Location_OnLeft, displayDock_);
    } else if (locationHint == QStringLiteral("right")) {
        addDockWidget(dock, KDDockWidgets::Location_OnRight, displayDock_);
    } else {
        addDockWidget(dock, KDDockWidgets::Location_OnBottom, displayDock_);
    }
#else
    auto *dock = new QDockWidget(title, this);
    dock->setObjectName(dockId);
    dock->setWidget(widget);
    docks_.insert(dockId, dock);
    Qt::DockWidgetArea area = Qt::BottomDockWidgetArea;
    if (locationHint == QStringLiteral("left")) {
        area = Qt::LeftDockWidgetArea;
    } else if (locationHint == QStringLiteral("right")) {
        area = Qt::RightDockWidgetArea;
    }
    QMainWindow::addDockWidget(area, dock);
#endif
}

void MainWindow::connectToServer() {
    const ConnectionTarget target = currentTarget();
    disconnectFromServer();
    updateTargetSummary();
    machineLabel_->setText(tr("Loading..."));
    QString probeError;
    if (!probeServerAvailable(target, &probeError)) {
        disconnectFromServer();
        machineLabel_->setText(tr("Not connected"));
        statusLabel_->setText(tr("Disconnected"));
        showError(probeError.isEmpty() ? tr("No server at %1").arg(target.address()) : probeError);
        return;
    }
    videoClient_->connectToTarget(target);
    keyboardClient_->connectToTarget(target);
    audioClient_->connectToTarget(target);
    systemClient_->fetchSystemInfo(target);
    indicatorClient_->connectToTarget(target);
    discClient_->connectToTarget(target);
    sidewaysClient_->connectToTarget(target);
    serialClient_->connectToTarget(target);
    videoWidget_->setFocus();
    saveUiState();
}

void MainWindow::disconnectFromServer() {
    videoClient_->disconnectFromTarget();
    keyboardClient_->disconnectFromTarget();
    audioClient_->disconnectFromTarget();
    indicatorClient_->disconnectFromTarget();
    discClient_->disconnectFromTarget();
    sidewaysClient_->disconnectFromTarget();
    serialClient_->disconnectFromTarget();
}

bool MainWindow::probeServerAvailable(const ConnectionTarget &target, QString *errorMessage) const {
    auto channel = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    auto stub = beebium::SystemService::NewStub(channel);

    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(1200));
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;
    const grpc::Status status = stub->GetSystemInfo(&context, request, &response);
    if (status.ok()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = tr("No server at %1 (%2)").arg(target.address(), QString::fromStdString(status.error_message()));
    }
    return false;
}

void MainWindow::updateMachineSummary(const QString &summary) {
    machineLabel_->setText(summary);
    if (applyingConfig_) {
        applyingConfig_ = false;
        pendingReconnectAttempts_ = 0;
        statusBar()->showMessage(tr("Connected using selected config."), 4000);
    }
}

void MainWindow::showError(const QString &message) {
    if (applyingConfig_) {
        statusBar()->showMessage(tr("Waiting for local server to become ready..."), 2000);
        return;
    }
    QMessageBox::warning(this, tr("Beebium Linux"), message);
}

void MainWindow::showAudioStatus(const QString &message) {
    statusBar()->showMessage(message, 5000);
    updateAudioDetails(message);
}

void MainWindow::resetMachine() {
    if (!keyboardClient_->breakDown()) {
        return;
    }
    keyboardClient_->breakUp();
    statusBar()->showMessage(tr("Reset sent"), 3000);
}

void MainWindow::attemptConfigReconnect() {
    if (!applyingConfig_) {
        return;
    }
    if (pendingReconnectAttempts_ <= 0) {
        applyingConfig_ = false;
        showError(tr("Timed out waiting for the local server to start."));
        return;
    }
    --pendingReconnectAttempts_;
    connectToServer();
    if (applyingConfig_) {
        QTimer::singleShot(700, this, &MainWindow::attemptConfigReconnect);
    }
}

void MainWindow::refreshIndicatorsView() {
    const auto &indicators = indicatorClient_->indicators();
    indicatorTable_->setRowCount(indicators.size());
    for (int row = 0; row < indicators.size(); ++row) {
        const auto &indicator = indicators.at(row);
        auto *labelItem = new QTableWidgetItem(indicator.label);
        if (!indicator.color.isEmpty()) {
            labelItem->setToolTip(indicator.color);
        }
        indicatorTable_->setItem(row, 0, labelItem);
        indicatorTable_->setItem(row, 1, new QTableWidgetItem(QString::number(indicator.value)));
        indicatorTable_->setItem(row, 2, new QTableWidgetItem(indicator.relatedKey));
        if (indicator.name == QStringLiteral("caps-lock-led")
            && (indicator.value == 0 || indicator.value == 255)) {
            qInfo(mainUiLog).noquote() << QStringLiteral("[caps] indicator=") << indicator.value;
            videoWidget_->setBbcCapsLockState(indicator.value == 255);
        }
    }
    indicatorTable_->resizeColumnsToContents();
}

void MainWindow::refreshStorageView() {
    const auto &drives = discClient_->drives();
    storageTable_->setRowCount(drives.size());
    for (int row = 0; row < drives.size(); ++row) {
        const auto &drive = drives.at(row);
        storageTable_->setItem(row, 0, new QTableWidgetItem(QString::number(drive.drive)));
        storageTable_->setItem(row, 1, new QTableWidgetItem(drive.state));
        storageTable_->setItem(row, 2, new QTableWidgetItem(drive.discName));
        storageTable_->setItem(row, 3, new QTableWidgetItem(drive.motorOn ? tr("On") : tr("Off")));
        storageTable_->setItem(row, 4, new QTableWidgetItem(QString::number(drive.currentTrack)));
        storageTable_->setItem(row, 5, new QTableWidgetItem(drive.writeProtected ? tr("Yes") : tr("No")));
    }
    controllerLabel_->setText(discClient_->hasDiscController()
                                  ? tr("%1").arg(discClient_->controllerType())
                                  : tr("No controller"));
    storageTable_->resizeColumnsToContents();
}

void MainWindow::refreshSidewaysView() {
    aliasingLabel_->setText(sidewaysClient_->hasAliasing() ? tr("Aliased") : tr("Independent"));
}

void MainWindow::refreshSerialView() {
    const auto &status = serialClient_->status();
    serialStateLabel_->setText(status.hasSerialSocket
                                   ? tr("TX %1 baud, RX %2 baud").arg(status.txBaud).arg(status.rxBaud)
                                   : tr("Machine has no serial socket"));
    const QString endpointState = status.endpointPath.isEmpty()
        ? tr("No endpoint")
        : tr("%1 (%2, %3)")
              .arg(status.endpointPath,
                   serialModeName(status.endpointMode),
                   status.endpointOpen ? tr("open") : tr("closed"));
    serialEndpointLabel_->setText(endpointState);
    serialQueueLabel_->setText(tr("TX %1 / RX %2").arg(status.txPending).arg(status.rxPending));
    if (!serialControlsDirty_) {
        serialUiUpdating_ = true;
        const int index = serialModeCombo_->findData(status.endpointMode);
        if (index >= 0) {
            serialModeCombo_->setCurrentIndex(index);
        }
        if (!status.endpointPath.isEmpty()) {
            serialPathEdit_->setText(status.endpointPath);
        }
        serialUiUpdating_ = false;
    }
    const int hostBaud = serialBaudCombo_->currentData().toInt();
    serialHostBaudLabel_->setText(hostBaud == 0 ? tr("Default (19200)") : QString::number(hostBaud));
}

ConnectionTarget MainWindow::currentTarget() const {
    ConnectionTarget target;
    target.host = hostEdit_->text().trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : hostEdit_->text().trimmed();
    target.port = portEdit_->text().toInt();
    if (target.port <= 0) {
        target.port = 48875;
    }
    return target;
}

int MainWindow::selectedDrive() const {
    const auto items = storageTable_->selectionModel() ? storageTable_->selectionModel()->selectedRows() : QModelIndexList();
    if (items.isEmpty()) {
        return -1;
    }
    return storageTable_->item(items.first().row(), 0)->text().toInt();
}

int MainWindow::selectedSidewaysSlot() const {
    const auto items = sidewaysTable_->selectionModel() ? sidewaysTable_->selectionModel()->selectedRows() : QModelIndexList();
    if (items.isEmpty()) {
        return -1;
    }
    return sidewaysTable_->item(items.first().row(), 0)->data(Qt::UserRole).toInt();
}
