#include "SettingsDialog.h"

#include "PluginInfo.h"
#include "PluginPaths.h"
#include "RemixConnector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace InstaMAT2Remix {
namespace {
constexpr const char* kSettingsOrg = "InstaMAT2Remix";
constexpr const char* kSettingsApp = "Config";

constexpr const char* kKeyApiBaseUrl = "RemixApiBaseUrl";
constexpr const char* kKeyPollTimeoutSec = "PollTimeoutSec";
constexpr const char* kKeyLogLevel = "LogLevel";

constexpr const char* kKeyBlenderPath = "BlenderPath";
constexpr const char* kKeyTexconvPath = "TexconvPath";
constexpr const char* kKeyExportBaseFolder = "ExportBaseFolder";
constexpr const char* kKeyRemixOutputSubfolder = "RemixOutputSubfolder";
constexpr const char* kKeyProjectsFolder = "ProjectsFolder";

constexpr const char* kKeyAutoUnwrap = "AutoUnwrap";
constexpr const char* kKeyUseTilingMesh = "UseTilingMeshOnPull";
constexpr const char* kKeyTilingMeshPath = "TilingMeshPath";
constexpr const char* kKeyAutoImportAfterPull = "AutoImportTexturesAfterPull";
constexpr const char* kKeyTemplateGraphPath = "TemplateGraphPath";
constexpr const char* kKeyTemplateMeshInput = "TemplateMeshInputName";
constexpr const char* kKeyTemplateAutoRun = "TemplateAutoRunOnPull";
constexpr const char* kKeyUvAngle = "BlenderSmartUVAngleLimit";
constexpr const char* kKeyUvMargin = "BlenderSmartUVIslandMargin";
constexpr const char* kKeyUvArea = "BlenderSmartUVAreaWeight";
constexpr const char* kKeyUvStretch = "BlenderSmartUVStretchToBounds";

constexpr const char* kKeyExportWidth = "ExportWidth";
constexpr const char* kKeyExportHeight = "ExportHeight";
constexpr const char* kKeyExportFormat = "ExportFormat";
constexpr const char* kKeyIncludeOpacity = "IncludeOpacityMap";

QString defaultTexconvPath() {
    const QString candidate = QDir(GetPluginDirPath()).filePath("texconv.exe");
    return QFileInfo::exists(candidate) ? candidate : QString();
}

QString defaultTilingMeshPath() {
    QStringList candidates;

    const QString base = GetPluginDirPath();
    candidates << QDir(base).filePath("assets/meshes/plane_tiling.usd");
    candidates << QDir(base).filePath("plane_tiling.usd");

    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!docs.isEmpty()) {
        candidates << QDir(docs).filePath("InstaMAT/Plugins/assets/meshes/plane_tiling.usd");
        candidates << QDir(docs).filePath("InstaMAT/Plugins/plane_tiling.usd");
    }

    const QString appdata = QString::fromLocal8Bit(qgetenv("APPDATA"));
    if (!appdata.isEmpty()) {
        candidates << QDir(appdata).filePath("InstaMAT Studio/Plugins/assets/meshes/plane_tiling.usd");
        candidates << QDir(appdata).filePath("InstaMAT Studio/Plugins/plane_tiling.usd");
    }

    const QString exeDir = QCoreApplication::applicationDirPath();
    candidates << QDir(exeDir).filePath("assets/meshes/plane_tiling.usd");
    candidates << QDir(exeDir).filePath("plane_tiling.usd");

    for (const QString& p : candidates) {
        const QString c = QDir::cleanPath(p);
        if (QFileInfo::exists(c)) return c;
    }
    return QString();
}

QString defaultExportBaseFolder() {
    const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(tempRoot).filePath("InstaMAT2Remix/Export");
}

QString defaultProjectsFolder() {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath("InstaMAT/Projects");
}
} // namespace

SettingsDialog::SettingsDialog(RemixConnector* connector, QWidget* parent)
    : QDialog(parent), m_connector(connector) {
    buildUi();
    loadFromSettings();
}

void SettingsDialog::buildUi() {
    setWindowTitle(QString("%1 - Settings").arg(kPluginName));
    setMinimumWidth(800);
    setMinimumHeight(560);

    auto* root = new QVBoxLayout(this);

    auto* header = new QLabel(QString("<b>%1</b> <span style='color:#888'>v%2</span>")
                                  .arg(kPluginName)
                                  .arg(kPluginVersion),
                              this);
    root->addWidget(header);

    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);

    // --- Connection tab ---
    {
        auto* w = new QWidget(this);
        auto* layout = new QFormLayout(w);

        m_apiBaseUrl = new QLineEdit(w);
        m_apiBaseUrl->setPlaceholderText(kDefaultApiBaseUrl);
        layout->addRow("API Base URL", m_apiBaseUrl);

        m_pollTimeout = new QDoubleSpinBox(w);
        m_pollTimeout->setRange(0.2, 300.0);
        m_pollTimeout->setDecimals(1);
        m_pollTimeout->setSingleStep(0.5);
        layout->addRow("Request Timeout (sec)", m_pollTimeout);

        auto* testRow = new QHBoxLayout();
        auto* testBtn = new QPushButton("Test Connection", w);
        m_testResult = new QLabel("", w);
        m_testResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
        testRow->addWidget(testBtn);
        testRow->addWidget(m_testResult, 1);
        layout->addRow(" ", testRow);
        connect(testBtn, &QPushButton::clicked, this, &SettingsDialog::onTestConnection);

        m_tabs->addTab(w, "Connection");
    }

    // --- Paths tab ---
    {
        auto* w = new QWidget(this);
        auto* layout = new QFormLayout(w);

        auto withBrowse = [&](QLineEdit* edit, const QString& title, const QString& filter, auto slot) {
            auto* host = new QWidget(w);
            auto* row = new QHBoxLayout(host);
            row->setContentsMargins(0, 0, 0, 0);
            auto* btn = new QPushButton("Browse...", host);
            row->addWidget(edit, 1);
            row->addWidget(btn);
            connect(btn, &QPushButton::clicked, this, slot);
            Q_UNUSED(title);
            Q_UNUSED(filter);
            return host;
        };

        m_texconvPath = new QLineEdit(w);
        layout->addRow("texconv.exe", withBrowse(m_texconvPath, "Select texconv.exe", "Executables (*.exe);;All Files (*)", &SettingsDialog::onBrowseTexconv));

        m_blenderPath = new QLineEdit(w);
        layout->addRow("Blender Executable", withBrowse(m_blenderPath, "Select blender.exe", "Executables (*.exe);;All Files (*)", &SettingsDialog::onBrowseBlender));

        m_exportFolder = new QLineEdit(w);
        layout->addRow("Export Folder", withBrowse(m_exportFolder, "Select export folder", "", &SettingsDialog::onBrowseExportFolder));

        m_projectsFolder = new QLineEdit(w);
        layout->addRow("Projects Folder", withBrowse(m_projectsFolder, "Select projects folder", "", &SettingsDialog::onBrowseProjectsFolder));

        m_remixOutputSubfolder = new QLineEdit(w);
        layout->addRow("Remix Output Subfolder", m_remixOutputSubfolder);

        // Log file (read-only)
        auto* logHost = new QWidget(w);
        auto* logRow = new QHBoxLayout(logHost);
        logRow->setContentsMargins(0, 0, 0, 0);
        m_logFilePath = new QLineEdit(w);
        m_logFilePath->setReadOnly(true);
        auto* openLogBtn = new QPushButton("Open Folder", w);
        logRow->addWidget(m_logFilePath, 1);
        logRow->addWidget(openLogBtn);
        layout->addRow("Plugin Log", logHost);
        connect(openLogBtn, &QPushButton::clicked, this, &SettingsDialog::onOpenLogFolder);

        m_tabs->addTab(w, "Paths");
    }

    // --- Pull tab ---
    {
        auto* w = new QWidget(this);
        auto* layout = new QFormLayout(w);

        m_autoUnwrap = new QCheckBox("Auto-unwrap pulled meshes with Blender (Smart UV Project)", w);
        layout->addRow(m_autoUnwrap);

        m_useTilingMesh = new QCheckBox("Use simple tiling mesh instead of selected mesh (fast PBR prep)", w);
        layout->addRow(m_useTilingMesh);

        auto* tilingHost = new QWidget(w);
        auto* tilingRow = new QHBoxLayout(tilingHost);
        tilingRow->setContentsMargins(0, 0, 0, 0);
        m_tilingMeshPath = new QLineEdit(w);
        auto* browseTiling = new QPushButton("Browse...", w);
        tilingRow->addWidget(m_tilingMeshPath, 1);
        tilingRow->addWidget(browseTiling);
        layout->addRow("Tiling Mesh Path", tilingHost);
        connect(browseTiling, &QPushButton::clicked, this, &SettingsDialog::onBrowseTilingMesh);

        m_autoImportAfterPull = new QCheckBox("Auto-import textures from Remix immediately after Pull", w);
        layout->addRow(m_autoImportAfterPull);

        m_templateAutoRun = new QCheckBox("Auto-run template PBR graph after Pull (uses exposed mesh path input)", w);
        layout->addRow(m_templateAutoRun);

        auto* templateHost = new QWidget(w);
        auto* templateRow = new QHBoxLayout(templateHost);
        templateRow->setContentsMargins(0, 0, 0, 0);
        m_templateGraphPath = new QLineEdit(w);
        auto* browseTemplate = new QPushButton("Browse...", w);
        templateRow->addWidget(m_templateGraphPath, 1);
        templateRow->addWidget(browseTemplate);
        layout->addRow("Template Project (.IMP)", templateHost);
        connect(browseTemplate, &QPushButton::clicked, this, &SettingsDialog::onBrowseTemplateGraph);

        m_templateMeshInputName = new QLineEdit(w);
        layout->addRow("Template Mesh Input Name", m_templateMeshInputName);

        m_uvAngleLimit = new QDoubleSpinBox(w);
        m_uvAngleLimit->setRange(0.0, 89.0);
        m_uvAngleLimit->setDecimals(1);
        layout->addRow("Smart UV Angle Limit", m_uvAngleLimit);

        m_uvIslandMargin = new QDoubleSpinBox(w);
        m_uvIslandMargin->setRange(0.0, 1.0);
        m_uvIslandMargin->setDecimals(4);
        m_uvIslandMargin->setSingleStep(0.0005);
        layout->addRow("Smart UV Island Margin", m_uvIslandMargin);

        m_uvAreaWeight = new QDoubleSpinBox(w);
        m_uvAreaWeight->setRange(0.0, 10.0);
        m_uvAreaWeight->setDecimals(3);
        layout->addRow("Smart UV Area Weight", m_uvAreaWeight);

        m_uvStretch = new QCheckBox("Stretch to Bounds", w);
        layout->addRow(m_uvStretch);

        m_tabs->addTab(w, "Pull");
    }

    // --- Export tab ---
    {
        auto* w = new QWidget(this);
        auto* layout = new QFormLayout(w);

        auto* resRow = new QWidget(w);
        auto* resLayout = new QHBoxLayout(resRow);
        resLayout->setContentsMargins(0, 0, 0, 0);
        m_exportWidth = new QSpinBox(w);
        m_exportWidth->setRange(16, 16384);
        m_exportWidth->setSuffix(" px");
        m_exportHeight = new QSpinBox(w);
        m_exportHeight->setRange(16, 16384);
        m_exportHeight->setSuffix(" px");
        resLayout->addWidget(m_exportWidth);
        resLayout->addWidget(new QLabel("x", w));
        resLayout->addWidget(m_exportHeight);
        layout->addRow("Export Resolution", resRow);

        m_exportFormat = new QComboBox(w);
        m_exportFormat->addItems({"png", "tga", "jpg"});
        layout->addRow("Export Format", m_exportFormat);

        m_includeOpacity = new QCheckBox("Export & push separate Opacity texture (optional)", w);
        layout->addRow(m_includeOpacity);

        m_tabs->addTab(w, "Export");
    }

    // --- Advanced tab ---
    {
        auto* w = new QWidget(this);
        auto* layout = new QFormLayout(w);

        m_logLevel = new QComboBox(w);
        m_logLevel->addItems({"debug", "info", "warning", "error"});
        layout->addRow("Log Level", m_logLevel);

        m_tabs->addTab(w, "Advanced");
    }

    // Buttons
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch(1);

    m_resetBtn = new QPushButton("Reset to Defaults", this);
    m_cancelBtn = new QPushButton("Cancel", this);
    m_okBtn = new QPushButton("OK", this);
    m_okBtn->setDefault(true);

    btnRow->addWidget(m_resetBtn);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_okBtn);
    root->addLayout(btnRow);

    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsDialog::onResetDefaults);
    connect(m_cancelBtn, &QPushButton::clicked, this, &SettingsDialog::reject);
    connect(m_okBtn, &QPushButton::clicked, this, &SettingsDialog::onAccept);
}

void SettingsDialog::loadFromSettings() {
    QSettings s(kSettingsOrg, kSettingsApp);

    m_apiBaseUrl->setText(s.value(kKeyApiBaseUrl, kDefaultApiBaseUrl).toString());
    m_pollTimeout->setValue(s.value(kKeyPollTimeoutSec, 60.0).toDouble());

    m_blenderPath->setText(s.value(kKeyBlenderPath, "").toString());
    m_texconvPath->setText(s.value(kKeyTexconvPath, defaultTexconvPath()).toString());
    m_exportFolder->setText(s.value(kKeyExportBaseFolder, defaultExportBaseFolder()).toString());
    m_projectsFolder->setText(s.value(kKeyProjectsFolder, defaultProjectsFolder()).toString());
    m_remixOutputSubfolder->setText(s.value(kKeyRemixOutputSubfolder, "Textures/InstaMAT2Remix_Ingested").toString());

    m_autoUnwrap->setChecked(s.value(kKeyAutoUnwrap, false).toBool());
    m_useTilingMesh->setChecked(s.value(kKeyUseTilingMesh, false).toBool());
    m_tilingMeshPath->setText(s.value(kKeyTilingMeshPath, defaultTilingMeshPath()).toString());
    m_autoImportAfterPull->setChecked(s.value(kKeyAutoImportAfterPull, true).toBool());
    m_templateAutoRun->setChecked(s.value(kKeyTemplateAutoRun, true).toBool());
    m_templateGraphPath->setText(s.value(kKeyTemplateGraphPath, "").toString());
    m_templateMeshInputName->setText(s.value(kKeyTemplateMeshInput, "InputMeshPath").toString());
    m_uvAngleLimit->setValue(s.value(kKeyUvAngle, 66.0).toDouble());
    m_uvIslandMargin->setValue(s.value(kKeyUvMargin, 0.003).toDouble());
    m_uvAreaWeight->setValue(s.value(kKeyUvArea, 0.0).toDouble());
    m_uvStretch->setChecked(s.value(kKeyUvStretch, false).toBool());

    m_exportWidth->setValue(s.value(kKeyExportWidth, 2048).toInt());
    m_exportHeight->setValue(s.value(kKeyExportHeight, 2048).toInt());

    const QString fmt = s.value(kKeyExportFormat, "png").toString().trimmed().toLower();
    const int fmtIdx = m_exportFormat->findText(fmt);
    if (fmtIdx >= 0) m_exportFormat->setCurrentIndex(fmtIdx);

    m_includeOpacity->setChecked(s.value(kKeyIncludeOpacity, false).toBool());

    const QString ll = s.value(kKeyLogLevel, "info").toString().trimmed().toLower();
    const int llIdx = m_logLevel->findText(ll);
    if (llIdx >= 0) m_logLevel->setCurrentIndex(llIdx);

    if (m_connector) m_logFilePath->setText(m_connector->GetLogFilePath());
}

void SettingsDialog::writeToSettings() {
    QSettings s(kSettingsOrg, kSettingsApp);

    s.setValue(kKeyApiBaseUrl, m_apiBaseUrl->text().trimmed());
    s.setValue(kKeyPollTimeoutSec, m_pollTimeout->value());
    s.setValue(kKeyLogLevel, m_logLevel->currentText());

    s.setValue(kKeyBlenderPath, m_blenderPath->text().trimmed());
    s.setValue(kKeyTexconvPath, m_texconvPath->text().trimmed());
    s.setValue(kKeyExportBaseFolder, m_exportFolder->text().trimmed());
    s.setValue(kKeyProjectsFolder, m_projectsFolder->text().trimmed());
    s.setValue(kKeyRemixOutputSubfolder, m_remixOutputSubfolder->text().trimmed());

    s.setValue(kKeyAutoUnwrap, m_autoUnwrap->isChecked());
    s.setValue(kKeyUseTilingMesh, m_useTilingMesh->isChecked());
    s.setValue(kKeyTilingMeshPath, m_tilingMeshPath->text().trimmed());
    s.setValue(kKeyAutoImportAfterPull, m_autoImportAfterPull->isChecked());
    s.setValue(kKeyTemplateAutoRun, m_templateAutoRun->isChecked());
    s.setValue(kKeyTemplateGraphPath, m_templateGraphPath->text().trimmed());
    s.setValue(kKeyTemplateMeshInput, m_templateMeshInputName->text().trimmed());
    s.setValue(kKeyUvAngle, m_uvAngleLimit->value());
    s.setValue(kKeyUvMargin, m_uvIslandMargin->value());
    s.setValue(kKeyUvArea, m_uvAreaWeight->value());
    s.setValue(kKeyUvStretch, m_uvStretch->isChecked());

    s.setValue(kKeyExportWidth, m_exportWidth->value());
    s.setValue(kKeyExportHeight, m_exportHeight->value());
    s.setValue(kKeyExportFormat, m_exportFormat->currentText());
    s.setValue(kKeyIncludeOpacity, m_includeOpacity->isChecked());
}

void SettingsDialog::applyDefaultsToUi() {
    m_apiBaseUrl->setText(kDefaultApiBaseUrl);
    m_pollTimeout->setValue(60.0);
    m_logLevel->setCurrentText("info");

    m_blenderPath->setText("");
    m_texconvPath->setText(defaultTexconvPath());
    m_exportFolder->setText(defaultExportBaseFolder());
    m_projectsFolder->setText(defaultProjectsFolder());
    m_remixOutputSubfolder->setText("Textures/InstaMAT2Remix_Ingested");

    m_autoUnwrap->setChecked(false);
    m_useTilingMesh->setChecked(false);
    m_tilingMeshPath->setText(defaultTilingMeshPath());
    m_autoImportAfterPull->setChecked(true);
    m_templateAutoRun->setChecked(true);
    m_templateGraphPath->setText("");
    m_templateMeshInputName->setText("InputMeshPath");
    m_uvAngleLimit->setValue(66.0);
    m_uvIslandMargin->setValue(0.003);
    m_uvAreaWeight->setValue(0.0);
    m_uvStretch->setChecked(false);

    m_exportWidth->setValue(2048);
    m_exportHeight->setValue(2048);
    m_exportFormat->setCurrentText("png");
    m_includeOpacity->setChecked(false);

    m_testResult->setText("");
}

void SettingsDialog::onBrowseBlender() {
    const QString path = QFileDialog::getOpenFileName(this, "Select blender.exe", m_blenderPath->text(), "Executables (*.exe);;All Files (*)");
    if (!path.isEmpty()) m_blenderPath->setText(path);
}

void SettingsDialog::onBrowseTexconv() {
    const QString path = QFileDialog::getOpenFileName(this, "Select texconv.exe", m_texconvPath->text(), "Executables (*.exe);;All Files (*)");
    if (!path.isEmpty()) m_texconvPath->setText(path);
}

void SettingsDialog::onBrowseExportFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, "Select export folder", m_exportFolder->text());
    if (!path.isEmpty()) m_exportFolder->setText(path);
}

void SettingsDialog::onBrowseProjectsFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, "Select projects folder", m_projectsFolder->text());
    if (!path.isEmpty()) m_projectsFolder->setText(path);
}

void SettingsDialog::onBrowseTilingMesh() {
    const QString path = QFileDialog::getOpenFileName(this, "Select tiling mesh", m_tilingMeshPath->text(), "Meshes (*.usd *.usda *.usdc *.obj *.fbx *.gltf *.glb);;All Files (*)");
    if (!path.isEmpty()) m_tilingMeshPath->setText(path);
}

void SettingsDialog::onBrowseTemplateGraph() {
    const QString path = QFileDialog::getOpenFileName(this,
                                                     "Select template IMP",
                                                     m_templateGraphPath->text(),
                                                     "InstaMAT Packages (*.imp *.IMP);;All Files (*)");
    if (!path.isEmpty()) m_templateGraphPath->setText(path);
}

void SettingsDialog::onTestConnection() {
    if (!m_connector) {
        m_testResult->setText("No connector instance available.");
        return;
    }

    // Persist current UI values so the test uses them.
    writeToSettings();
    m_connector->ReloadSettings();

    QString msg;
    const bool ok = m_connector->TestConnection(msg);
    m_testResult->setText(ok ? ("OK: " + msg) : ("FAILED: " + msg));
}

void SettingsDialog::onOpenLogFolder() {
    const QString path = m_logFilePath ? m_logFilePath->text() : QString();
    if (path.isEmpty()) return;

    const QString folder = QFileInfo(path).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void SettingsDialog::onResetDefaults() {
    applyDefaultsToUi();
}

void SettingsDialog::onAccept() {
    const QUrl url(m_apiBaseUrl->text().trimmed());
    if (url.scheme().compare("http", Qt::CaseInsensitive) == 0) {
        const QString host = url.host().toLower();
        if (host != "localhost" && host != "127.0.0.1") {
            const auto result = QMessageBox::warning(this, "Security Warning",
                "You are using an unencrypted HTTP connection to a remote API host.\n\n"
                "This can expose sensitive data and is not recommended. Use HTTPS instead if the remote host supports it.\n\n"
                "Do you want to proceed anyway?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (result != QMessageBox::Yes) return;
        }
    }

    writeToSettings();
    if (m_connector) m_connector->ReloadSettings();
    accept();
}

} // namespace InstaMAT2Remix


