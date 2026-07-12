#include "SettingsDialog.h"

#include "PluginInfo.h"
#include "PluginPaths.h"
#include "ProjectTemplates.h"
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
constexpr const char* kKeyRemixOutputSubfolder = "RemixOutputSubfolder";

constexpr const char* kKeyAutoUnwrap = "AutoUnwrap";
constexpr const char* kKeyUseTilingMesh = "UseTilingMeshOnPull";
constexpr const char* kKeyTilingMeshPath = "TilingMeshPath";
constexpr const char* kKeyUvAngle = "BlenderSmartUVAngleLimit";
constexpr const char* kKeyUvMargin = "BlenderSmartUVIslandMargin";
constexpr const char* kKeyUvArea = "BlenderSmartUVAreaWeight";
constexpr const char* kKeyUvStretch = "BlenderSmartUVStretchToBounds";

constexpr const char* kKeyExportFolder = "ExportFolder";
constexpr const char* kKeyExportFileFormat = "ExportFileFormat";
constexpr const char* kKeyExportResolution = "ExportResolution";
constexpr const char* kKeyIncludeOpacityMap = "IncludeOpacityMap";
constexpr const char* kKeyRestoreAspect = "RestoreAspectOnExport";
constexpr const char* kKeyPullTemplate = "PullProjectTemplate";
constexpr const char* kKeyNormalEncoding = "NormalMapEncoding";

QString defaultTexconvPath() {
    return DetectTexconvPath();
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

QString defaultExportFolder() {
    // Temp-rooted like WBC's %TEMP%/RemixConnector_Export: Remix's
    // TextureImporter only stages source files that live in transient paths.
    return QDir::cleanPath(QDir::tempPath() + "/InstaMAT2Remix_Export");
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

    // Tab order mirrors Substance2Remix: Connection / Paths / Pull / Export / Advanced.
    buildConnectionTab();

    buildPathsTab();

    buildPullTab();

    buildExportTab();

    buildAdvancedTab();

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

void SettingsDialog::buildConnectionTab() {
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

void SettingsDialog::buildPathsTab() {
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
        m_exportFolder->setToolTip(
            "This folder is EMPTIED before every Push, so use a dedicated folder.\n"
            "Leave empty to restore the default (a temp folder).");
        layout->addRow("Export Folder", withBrowse(m_exportFolder, "Select export folder", "", &SettingsDialog::onBrowseExportFolder));

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

void SettingsDialog::buildPullTab() {
        auto* w = new QWidget(this);
        auto* layout = new QFormLayout(w);

        m_pullTemplate = new QComboBox(w);
        m_pullTemplate->addItem("Ask each time", "ask");
        for (const TemplateRecipeConfig& cfg : AllTemplateConfigs()) {
            m_pullTemplate->addItem(QString::fromUtf8(cfg.displayName),
                                    QString::fromUtf8(cfg.settingsValue));
        }
        m_pullTemplate->setToolTip(
            "Which InstaMAT project template Pull From Remix creates.\n"
            "'Ask each time' shows a chooser on every Pull.");
        layout->addRow("Project Template on Pull", m_pullTemplate);

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

        m_tabs->addTab(w, "Pull");
}

void SettingsDialog::buildExportTab() {
        auto* w = new QWidget(this);
        auto* layout = new QFormLayout(w);

        m_exportFormat = new QComboBox(w);
        m_exportFormat->addItems({"png", "tga", "jpg"});
        layout->addRow("Export Format", m_exportFormat);

        // InstaMAT-specific: Painter derives resolution from the document,
        // InstaMAT's live export needs an explicit SetFormat size. Auto renders
        // at the resolution the project was BAKED at (BakeSettings in the .IMP).
        m_exportResolution = new QComboBox(w);
        m_exportResolution->addItem("Auto (project's baked resolution)", 0);
        m_exportResolution->setToolTip(
            "Auto pushes at the resolution you baked the project at.\n"
            "Pick a fixed size to override it.");
        m_exportResolution->addItem("512", 512);
        m_exportResolution->addItem("1024", 1024);
        m_exportResolution->addItem("2048", 2048);
        m_exportResolution->addItem("4096", 4096);
        layout->addRow("Export Resolution", m_exportResolution);

        m_normalEncoding = new QComboBox(w);
        m_normalEncoding->addItem("DirectX (default)", "dx");
        m_normalEncoding->addItem("OpenGL", "ogl");
        m_normalEncoding->addItem("Octahedral", "oth");
        m_normalEncoding->setToolTip(
            "How Remix should interpret the pushed normal map\n"
            "(sets the ingest type: NORMAL_DX / NORMAL_OGL / NORMAL_OTH).");
        layout->addRow("Normal Map Encoding", m_normalEncoding);

        m_includeOpacity = new QCheckBox("Merge Opacity into Base Color alpha on push", w);
        m_includeOpacity->setToolTip(
            "RTX Remix reads opacity from the diffuse texture's alpha channel.\n"
            "When enabled, the exported opacity map is merged into albedo's alpha\n"
            "before ingest. (A separate opacity texture is never pushed — Remix\n"
            "has no input for one.)");
        layout->addRow(m_includeOpacity);

        m_restoreAspect = new QCheckBox("Restore original texture proportions on export", w);
        layout->addRow(m_restoreAspect);

        m_tabs->addTab(w, "Export");
}

void SettingsDialog::buildAdvancedTab() {
        auto* w = new QWidget(this);
        auto* layout = new QFormLayout(w);

        m_logLevel = new QComboBox(w);
        m_logLevel->addItems({"debug", "info", "warning", "error"});
        layout->addRow("Log Level", m_logLevel);

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

        m_tabs->addTab(w, "Advanced");
}

void SettingsDialog::loadFromSettings() {
    QSettings s(kSettingsOrg, kSettingsApp);

    m_apiBaseUrl->setText(s.value(kKeyApiBaseUrl, kDefaultApiBaseUrl).toString());
    m_pollTimeout->setValue(s.value(kKeyPollTimeoutSec, 60.0).toDouble());

    m_blenderPath->setText(s.value(kKeyBlenderPath, "").toString());
    m_texconvPath->setText(s.value(kKeyTexconvPath, defaultTexconvPath()).toString());
    m_exportFolder->setText(s.value(kKeyExportFolder, defaultExportFolder()).toString());
    m_remixOutputSubfolder->setText(s.value(kKeyRemixOutputSubfolder, "Textures/InstaMAT2Remix_Ingested").toString());

    const QString tpl = s.value(kKeyPullTemplate, "ask").toString().trimmed().toLower();
    const int tplIdx = m_pullTemplate->findData(tpl);
    m_pullTemplate->setCurrentIndex(tplIdx >= 0 ? tplIdx : 0);

    m_autoUnwrap->setChecked(s.value(kKeyAutoUnwrap, false).toBool());
    m_useTilingMesh->setChecked(s.value(kKeyUseTilingMesh, false).toBool());
    m_tilingMeshPath->setText(s.value(kKeyTilingMeshPath, defaultTilingMeshPath()).toString());
    m_uvAngleLimit->setValue(s.value(kKeyUvAngle, 66.0).toDouble());
    m_uvIslandMargin->setValue(s.value(kKeyUvMargin, 0.003).toDouble());
    m_uvAreaWeight->setValue(s.value(kKeyUvArea, 0.0).toDouble());
    m_uvStretch->setChecked(s.value(kKeyUvStretch, false).toBool());

    const QString fmt = s.value(kKeyExportFileFormat, "png").toString().trimmed().toLower();
    const int fmtIdx = m_exportFormat->findText(fmt);
    m_exportFormat->setCurrentIndex(fmtIdx >= 0 ? fmtIdx : 0);

    const int res = s.value(kKeyExportResolution, 0).toInt();
    const int resIdx = m_exportResolution->findData(res);
    m_exportResolution->setCurrentIndex(resIdx >= 0 ? resIdx : 0);

    const QString enc = s.value(kKeyNormalEncoding, "dx").toString().trimmed().toLower();
    const int encIdx = m_normalEncoding->findData(enc);
    m_normalEncoding->setCurrentIndex(encIdx >= 0 ? encIdx : 0);

    m_includeOpacity->setChecked(s.value(kKeyIncludeOpacityMap, false).toBool());
    m_restoreAspect->setChecked(s.value(kKeyRestoreAspect, true).toBool());

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
    s.setValue(kKeyExportFolder, m_exportFolder->text().trimmed());
    s.setValue(kKeyRemixOutputSubfolder, m_remixOutputSubfolder->text().trimmed());

    s.setValue(kKeyPullTemplate, m_pullTemplate->currentData().toString());
    s.setValue(kKeyAutoUnwrap, m_autoUnwrap->isChecked());
    s.setValue(kKeyUseTilingMesh, m_useTilingMesh->isChecked());
    s.setValue(kKeyTilingMeshPath, m_tilingMeshPath->text().trimmed());
    s.setValue(kKeyUvAngle, m_uvAngleLimit->value());
    s.setValue(kKeyUvMargin, m_uvIslandMargin->value());
    s.setValue(kKeyUvArea, m_uvAreaWeight->value());
    s.setValue(kKeyUvStretch, m_uvStretch->isChecked());

    s.setValue(kKeyExportFileFormat, m_exportFormat->currentText());
    s.setValue(kKeyExportResolution, m_exportResolution->currentData().toInt());
    s.setValue(kKeyNormalEncoding, m_normalEncoding->currentData().toString());
    s.setValue(kKeyIncludeOpacityMap, m_includeOpacity->isChecked());
    s.setValue(kKeyRestoreAspect, m_restoreAspect->isChecked());
}

void SettingsDialog::applyDefaultsToUi() {
    m_apiBaseUrl->setText(kDefaultApiBaseUrl);
    m_pollTimeout->setValue(60.0);
    m_logLevel->setCurrentText("info");

    m_blenderPath->setText("");
    m_texconvPath->setText(defaultTexconvPath());
    m_exportFolder->setText(defaultExportFolder());
    m_remixOutputSubfolder->setText("Textures/InstaMAT2Remix_Ingested");

    m_pullTemplate->setCurrentIndex(0);            // Ask each time
    m_autoUnwrap->setChecked(false);
    m_useTilingMesh->setChecked(false);
    m_tilingMeshPath->setText(defaultTilingMeshPath());
    m_uvAngleLimit->setValue(66.0);
    m_uvIslandMargin->setValue(0.003);
    m_uvAreaWeight->setValue(0.0);
    m_uvStretch->setChecked(false);

    m_exportFormat->setCurrentIndex(0);            // png
    m_exportResolution->setCurrentIndex(0);        // Auto
    m_normalEncoding->setCurrentIndex(0);          // DirectX
    m_includeOpacity->setChecked(false);
    m_restoreAspect->setChecked(true);

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

void SettingsDialog::onBrowseTilingMesh() {
    const QString path = QFileDialog::getOpenFileName(this, "Select tiling mesh", m_tilingMeshPath->text(), "Meshes (*.usd *.usda *.usdc *.obj *.fbx *.gltf *.glb);;All Files (*)");
    if (!path.isEmpty()) m_tilingMeshPath->setText(path);
}

void SettingsDialog::onTestConnection() {
    if (!m_connector) {
        m_testResult->setText("No connector instance available.");
        return;
    }

    // Test with the CURRENT UI values, but do not let a test permanently
    // persist unsaved edits (Cancel must still cancel): snapshot every key
    // writeToSettings touches, write, test, then restore the snapshot.
    static const char* kManagedKeys[] = {
        kKeyApiBaseUrl, kKeyPollTimeoutSec, kKeyLogLevel,
        kKeyBlenderPath, kKeyTexconvPath, kKeyExportFolder, kKeyRemixOutputSubfolder,
        kKeyAutoUnwrap, kKeyUseTilingMesh, kKeyTilingMeshPath,
        kKeyUvAngle, kKeyUvMargin, kKeyUvArea, kKeyUvStretch,
        kKeyExportFileFormat, kKeyExportResolution, kKeyIncludeOpacityMap, kKeyRestoreAspect,
        kKeyPullTemplate, kKeyNormalEncoding,
    };
    QSettings s(kSettingsOrg, kSettingsApp);
    QHash<QString, QVariant> snapshot;
    QStringList absentKeys;
    for (const char* key : kManagedKeys) {
        if (s.contains(key)) snapshot.insert(key, s.value(key));
        else absentKeys << key;
    }

    writeToSettings();
    m_connector->ReloadSettings();

    m_testResult->setText("Testing...");
    QCoreApplication::processEvents();
    QString msg;
    const bool ok = m_connector->TestConnection(msg);

    for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
        s.setValue(it.key(), it.value());
    for (const QString& key : absentKeys) s.remove(key);
    m_connector->ReloadSettings();

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
    // A scheme-less URL ("localhost:8011") breaks every request with a
    // cryptic Qt error — normalize it to http:// before validating.
    QString apiUrl = m_apiBaseUrl->text().trimmed();
    if (!apiUrl.isEmpty() && !apiUrl.contains(QStringLiteral("://"))) {
        apiUrl = "http://" + apiUrl;
        m_apiBaseUrl->setText(apiUrl);
    }

    // The export folder is emptied on every Push; refuse folders that would
    // destroy user data, and restore the default when left empty.
    QString exportFolder = m_exportFolder->text().trimmed();
    if (exportFolder.isEmpty()) {
        exportFolder = defaultExportFolder();
        m_exportFolder->setText(exportFolder);
    }
    if (!RemixConnector::IsSafeToWipe(exportFolder)) {
        QMessageBox::warning(this, QString(kPluginName) + " - Settings",
            QString("The Export Folder is not safe to use:\n  %1\n\n"
                    "It is emptied on every Push, so it must be a dedicated folder — "
                    "not a drive root, your home folder, Documents, Desktop, "
                    "Downloads or Pictures.\n\n"
                    "Pick a different folder, or clear the field to use the default.")
                .arg(exportFolder));
        m_tabs->setCurrentIndex(1); // Paths tab
        m_exportFolder->setFocus();
        return;
    }

    const QUrl url(apiUrl);
    if (url.scheme().compare("http", Qt::CaseInsensitive) == 0) {
        const QString host = url.host().toLower();
        if (host != "localhost" && host != "127.0.0.1") {
            const auto result = QMessageBox::warning(this, QString(kPluginName) + " - Security Warning",
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


