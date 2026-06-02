#pragma once

#include <QHash>
#include <QVector>

#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/TitleBar.h>
#else
#include <QMainWindow>
#endif

#include "ConfigProfiles.hpp"
#include "ConnectionTarget.hpp"
#include "VideoWidget.hpp"

class GrpcDiscClient;
class GrpcIndicatorClient;
class GrpcSystemClient;
class GrpcKeyboardClient;
class GrpcAudioClient;
class GrpcSerialClient;
class GrpcSidewaysClient;
class GrpcVideoClient;
class ConfigsDialog;
class LocalServerManager;
class QLabel;
class QLineEdit;
class QComboBox;
class QDockWidget;
class QFormLayout;
class QGroupBox;
class QMenu;
class QActionGroup;
class QCheckBox;
class QSlider;
class QSpinBox;
class QTableWidget;
class QPushButton;
class QAction;
class QTextEdit;

#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
namespace KDDockWidgets {
class DockWidget;
class LayoutSaver;
}
#endif

#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
class MainWindow final : public KDDockWidgets::QtWidgets::MainWindow {
#else
class MainWindow final : public QMainWindow {
#endif
    Q_OBJECT

public:
    explicit MainWindow(const ConnectionTarget &initialTarget, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void connectToServer();
    void updateMachineSummary(const QString &summary);
    void showError(const QString &message);
    void showAudioStatus(const QString &message);
    void resetMachine();
    void disconnectUiSession();
    void attemptConfigReconnect();
    void refreshIndicatorsView();
    void refreshStorageView();
    void refreshSidewaysView();
    void refreshSerialView();
    void openConfigsWindow();
    void handleConfigsSaved(const QVector<struct ConfigProfile> &profiles, int currentIndex);
    void selectConfigProfile(int index);

private:
    void buildDockLayout();
    void buildMenus();
    QWidget *createConnectionPanel();
    QWidget *createStatusPanel();
    QWidget *createIndicatorsPanel();
    QWidget *createStoragePanel();
    QWidget *createConfigSummaryPanel();
    QWidget *createSerialPanel();
    QWidget *createAudioPanel();
    void restoreUiState();
    void saveUiState();
    void disconnectFromServer();
    [[nodiscard]] bool probeServerAvailable(const ConnectionTarget &target, QString *errorMessage = nullptr) const;
    void updateTargetSummary();
    void updateAudioDetails(const QString &message = QString());
    void updateConfigSummary();
    void rebuildHardwareMenu();
    void updateConnectionOwnershipUi();
    void updateDisplayAspect(VideoWidget::AspectMode mode);
    void updateTextureFilter(VideoWidget::TextureFilter filter);
    void addPanelDock(const QString &dockId, const QString &title, QWidget *widget, const QString &locationHint);
    void syncAudioDeviceChoices();
    void reconnectAudioStream();

    ConnectionTarget currentTarget() const;
    int selectedDrive() const;
    int selectedSidewaysSlot() const;

    QLineEdit *hostEdit_ = nullptr;
    QLineEdit *portEdit_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QPushButton *disconnectButton_ = nullptr;
    QPushButton *resetButton_ = nullptr;
    QLabel *machineLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *targetLabel_ = nullptr;
    QLabel *controllerLabel_ = nullptr;
    QLabel *aliasingLabel_ = nullptr;
    QLabel *audioStatusLabel_ = nullptr;
    QLabel *audioFormatLabel_ = nullptr;
    QLabel *audioStreamLabel_ = nullptr;
    QLabel *audioDeviceLabel_ = nullptr;
    QLabel *serialEndpointLabel_ = nullptr;
    QLabel *serialStateLabel_ = nullptr;
    QLabel *serialHostBaudLabel_ = nullptr;
    QLabel *serialQueueLabel_ = nullptr;
    QLabel *configSummaryLabel_ = nullptr;
    QLabel *connectionOwnershipLabel_ = nullptr;
    QCheckBox *keepServerRunningCheck_ = nullptr;
    QComboBox *audioDeviceCombo_ = nullptr;
    QSlider *audioVolumeSlider_ = nullptr;
    QTableWidget *indicatorTable_ = nullptr;
    QTableWidget *storageTable_ = nullptr;
    QTableWidget *sidewaysTable_ = nullptr;
    QComboBox *serialModeCombo_ = nullptr;
    QLineEdit *serialPathEdit_ = nullptr;
    QComboBox *serialBaudCombo_ = nullptr;
    VideoWidget *videoWidget_ = nullptr;
    GrpcVideoClient *videoClient_ = nullptr;
    GrpcKeyboardClient *keyboardClient_ = nullptr;
    GrpcAudioClient *audioClient_ = nullptr;
    GrpcSystemClient *systemClient_ = nullptr;
    GrpcIndicatorClient *indicatorClient_ = nullptr;
    GrpcDiscClient *discClient_ = nullptr;
    GrpcSidewaysClient *sidewaysClient_ = nullptr;
    GrpcSerialClient *serialClient_ = nullptr;
    ConfigsDialog *configsDialog_ = nullptr;
    LocalServerManager *localServerManager_ = nullptr;
    QVector<struct ConfigProfile> configProfiles_;
    int currentConfigIndex_ = 0;
    QMenu *hardwareMenu_ = nullptr;

#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
    QHash<QString, KDDockWidgets::QtWidgets::DockWidget *> docks_;
    KDDockWidgets::QtWidgets::DockWidget *displayDock_ = nullptr;
#else
    QHash<QString, QDockWidget *> docks_;
#endif

    bool serialControlsDirty_ = false;
    bool serialUiUpdating_ = false;
    bool applyingConfig_ = false;
    bool restoringUiState_ = false;
    bool keepServerRunningOnExit_ = false;
    int pendingReconnectAttempts_ = 0;
    QAction *keepRunningAction_ = nullptr;
    QActionGroup *displayAspectGroup_ = nullptr;
    QActionGroup *textureFilterGroup_ = nullptr;
};
