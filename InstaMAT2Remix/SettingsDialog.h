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
    void onBrowseExportFolder();
    void onBrowseTilingMesh();
    void onTestConnection();
    void onOpenLogFolder();
    void onResetDefaults();
    void onAccept();

private:
    void buildUi();
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
    QLineEdit* m_templateGraphPath = nullptr;
    QLineEdit* m_projectsFolder = nullptr;
    QLineEdit* m_templateMeshInputName = nullptr;

    // Pull
    QCheckBox* m_autoUnwrap = nullptr;
    QCheckBox* m_useTilingMesh = nullptr;
    QCheckBox* m_autoImportAfterPull = nullptr;
    QCheckBox* m_templateAutoRun = nullptr;
    QDoubleSpinBox* m_uvAngleLimit = nullptr;
    QDoubleSpinBox* m_uvIslandMargin = nullptr;
    QDoubleSpinBox* m_uvAreaWeight = nullptr;
    QCheckBox* m_uvStretch = nullptr;

    // Export
    QSpinBox* m_exportWidth = nullptr;
    QSpinBox* m_exportHeight = nullptr;
    QComboBox* m_exportFormat = nullptr;
    QCheckBox* m_includeOpacity = nullptr;

    // Advanced
    QComboBox* m_logLevel = nullptr;

    // Buttons
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_okBtn = nullptr;

private slots:
    void onBrowseTemplateGraph();
    void onBrowseProjectsFolder();
};

} // namespace InstaMAT2Remix


