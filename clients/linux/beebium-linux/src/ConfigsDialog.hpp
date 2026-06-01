#pragma once

#include <QDialog>
#include <QVector>

#include "ConfigProfiles.hpp"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QRadioButton;
class QTableWidget;

class ConfigsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ConfigsDialog(const QVector<ConfigProfile> &profiles, int currentIndex, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    void configsSaved(const QVector<ConfigProfile> &profiles, int currentIndex);

private slots:
    void newProfile();
    void duplicateProfile();
    void deleteProfile();
    void moveProfileUp();
    void moveProfileDown();
    void profileSelectionChanged();
    void chooseRomContent();
    void chooseHostOs();
    void saveAndClose();
    void requestClose();

private:
    void rebuildList();
    void loadProfileIntoEditor(int index);
    void storeEditorIntoCurrentProfile();
    bool saveProfiles();
    bool confirmClose();
    void markDirty();
    void addRecentRomPath(const QString &path);
    QStringList recentRomPaths() const;
    int currentRow() const;

    QVector<ConfigProfile> profiles_;
    bool loading_ = false;
    bool dirty_ = false;
    QListWidget *profileList_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLabel *modelLabel_ = nullptr;
    QLabel *discLabel_ = nullptr;
    QLineEdit *hostOsEdit_ = nullptr;
    QPushButton *hostOsButton_ = nullptr;
    QTableWidget *romTable_ = nullptr;
    QRadioButton *tubeNoneRadio_ = nullptr;
    QRadioButton *tube6502Radio_ = nullptr;
    QRadioButton *tubeTurboRadio_ = nullptr;
    QLineEdit *tubeOsEdit_ = nullptr;
    QComboBox *serialModeCombo_ = nullptr;
    QComboBox *serialBaudCombo_ = nullptr;
    QLineEdit *serialPathEdit_ = nullptr;
    QDialogButtonBox *buttonBox_ = nullptr;
};
