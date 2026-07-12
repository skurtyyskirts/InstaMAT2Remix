#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace InstaMAT2Remix {

class RemixConnector;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(RemixConnector* connector, QWidget* parent = nullptr);

private slots:
    void onBrowseBlender();
    void onBrowseTexconv();
    void onBrowseTilingMesh();
    void onBrowseExportFolder();
    void onTestConnection();
    void onOpenLogFolder();
    void onResetDefaults();
    void onAccept();

private:
    void buildUi();
    void buildConnectionTab();
    void buildPathsTab();
    void buildPullTab();
    void buildExportTab();
    void buildAdvancedTab();
    void loadFromSettings();
    void writeToSettings();
    void applyDefaultsToUi();

    RemixConnector* m_connector = nullptr;

    QTabWidget* m_tabs = nullptr;

    // Connection
    QLineEdit* m_apiBaseUrl = nullptr;
    QDoubleSpinBox* m_pollTimeout = nullptr;
    QLabel* m_testResult = nullptr;

    // Paths
    QLineEdit* m_texconvPath = nullptr;
    QLineEdit* m_blenderPath = nullptr;
    QLineEdit* m_exportFolder = nullptr;
    QLineEdit* m_remixOutputSubfolder = nullptr;
    QLineEdit* m_logFilePath = nullptr;
    QLineEdit* m_tilingMeshPath = nullptr;

    // Pull
    QComboBox* m_pullTemplate = nullptr;
    QCheckBox* m_autoUnwrap = nullptr;
    QCheckBox* m_useTilingMesh = nullptr;

    // Export
    QComboBox* m_exportFormat = nullptr;
    QComboBox* m_exportResolution = nullptr;
    QComboBox* m_normalEncoding = nullptr;
    QCheckBox* m_includeOpacity = nullptr;
    QCheckBox* m_restoreAspect = nullptr;

    // Advanced
    QComboBox* m_logLevel = nullptr;
    QDoubleSpinBox* m_uvAngleLimit = nullptr;
    QDoubleSpinBox* m_uvIslandMargin = nullptr;
    QDoubleSpinBox* m_uvAreaWeight = nullptr;
    QCheckBox* m_uvStretch = nullptr;

    // Buttons
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_okBtn = nullptr;
};

} // namespace InstaMAT2Remix


