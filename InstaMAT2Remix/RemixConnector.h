#pragma once
#include "InstaMATAPI.h"
#include "InstaMATPluginAPI.h"
#include "ExternalTools.h"
#include "Logger.h"
#include <string>
#include <vector>
#include <memory>
#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

class QProgressDialog;

namespace InstaMAT2Remix {
    
    class RemixConnector : public QObject {
        Q_OBJECT
    public:
        explicit RemixConnector(InstaMAT::IInstaMAT& instaMAT, QObject* parent = nullptr);
        ~RemixConnector();

        void SetRemixApiBaseUrl(const std::string& baseUrl);
        void ReloadSettings();

        QString GetLogFilePath() const;
        bool TestConnection(QString& outMessage) const;
        QString BuildDiagnosticsReport() const;

        // UI actions (labels/order mirror Substance2Remix)
        // Pull: resolve Remix selection -> optional tiling/unwrap -> auto-create
        // an InstaMAT project from the mesh and link it. Zero prompts; textures
        // are fetched separately by ImportTexturesFromRemix.
        void PullFromRemix();
        bool ImportTexturesFromRemix();
        // Push: live-export the painted layer project and ingest into Remix.
        // forceRelinkAndRename (Force Push) silently relinks to the current
        // Remix selection and stages under a non-overwriting filename root.
        void PushToRemix(bool forceRelinkAndRename);

        // InstaMAT2Duplicate: live-export the currently painted project (same
        // out-of-process worker path as Push — see ExportActiveLayeringProject)
        // and publish it to Remix as a NEW, independent material prim under a
        // fresh XXH3-64 identity. The linked/source material is never modified.
        // MDL texture bindings the source material carries but this export did
        // not re-bake are copied onto the new prim (best-effort; the Remix REST
        // surface has no generic non-texture MDL-scalar endpoint — see
        // GenerateMaterialHash / BuildDuplicateUsdaSidecar below).
        void DuplicateMaterialToRemix();

        ExternalTools& GetTools() { return m_tools; }

        static void SetInstance(RemixConnector* instance);
        static RemixConnector* GetInstance();

        // --- Headless Pull helpers and types (public so QtTest can access them
        // without `#define private public`, which MSVC mangles into the symbol
        // name for static and non-static member functions and breaks linkage
        // between test and impl translation units).

        // Returns the canonical PBR channel name for a USD attribute (e.g.
        // "inputs:diffuse_texture" -> "albedo"). Lookup is suffix-based:
        // the part of usdAttr after the LAST ':' is matched against the WBC
        // REMIX_ATTR_SUFFIX_TO_PBR_MAP table. Returns empty string for unknown
        // suffixes — callers fall back to the original filename.
        static QString ResolveCanonicalChannel(const QString& usdAttr);

        enum class NamingPolicy {
            PreserveOriginal,           // Keep Remix's filename. Used by ImportTexturesFromRemix.
            CanonicalChannelOrOriginal, // Apply WBC PBR table; fall back to original when no match.
        };

        struct ChannelEntry {
            QString usdAttr;   // e.g. "inputs:diffuse_texture"
            QString channel;   // e.g. "albedo"; empty when the WBC table did not match
            QString filename;  // sibling filename written into destDir
        };

        // Copies sourcePath into %TEMP%/InstaMAT2Remix_PreIngest/<destFileName>
        // (basename overload keeps the original name) and returns the staged
        // absolute path on success, empty string on failure (with reason in
        // outErr). Remix's TextureImporter context plugin only stages the
        // source file alongside its DDS output when the source lives in a
        // transient (temp-rooted) location; Documents-rooted sources produce
        // the .rtex.dds + .png.meta but no source copy, which fails Remix's
        // later hash check and leaves the material rendering gray.
        // Public so the QtTest can exercise it without `#define private public`.
        QString StageSourceForIngest(const QString& sourcePath, QString& outErr) const;
        QString StageSourceForIngest(const QString& sourcePath,
                                     const QString& destFileName,
                                     QString&       outErr) const;

        // --- Force Push non-overwriting root (port of WBC texture_processor) ---
        // Strips Windows-illegal filename characters + control chars.
        static QString SanitizeFilenameStem(const QString& stem);
        // Trailing-16-alphanumeric material hash of a prim path; empty if none.
        static QString DeriveDesiredRootFromPrim(const QString& materialPrim);
        // True when a .dds/.rtex.dds in ingestDirAbs starts with root at a
        // name boundary ('.', '_', '-' or exact). Case-insensitive.
        static bool ForcePushRootConflicts(const QString& root, const QString& ingestDirAbs);
        // First of root, root_1, root_2, … that does not conflict; falls back
        // to an epoch suffix after 9999 tries.
        static QString ChooseNonOverwritingRoot(const QString& desiredRoot,
                                                const QString& ingestDirAbs);

        // Returns the staging root directory %TEMP%/InstaMAT2Remix_PreIngest.
        // PushToRemix wipes this before each push so a previous run's leftovers
        // don't confuse Remix's FileCleanup resultor.
        static QString PreIngestStageDir();

        // Returns the default Studio layer-project directory:
        // %USERPROFILE%/Documents/InstaMAT/Library. The recipe's "New Project"
        // dialog saves unsaved Layering projects here, so Push tails this
        // directory to find the live project when GetGraphByName cannot see
        // it (e.g. Studio has registered the package but not its graph names).
        // Used to be DefaultAutosaveDir / .../AutoSave — that directory was
        // empty in practice; see CLAUDE.md handoff for the 2026-05-14 forensic
        // result that ruled out autosave.
        static QString DefaultLayerProjectDir();

        // Picks the .IMP file with the newest mtime under dirPath and returns
        // its absolute path + mtime. Skips the plugin's own InstaMAT2Remix.IMP
        // (which also lives in the Library and is not a layer project).
        // Returns false with outErr set if the dir is missing or contains no
        // candidate .IMP files. Pure file-system logic, public so the QtTest
        // can drive it without `#define private public`.
        bool FindMostRecentLayerPackageIn(const QString& dirPath,
                                          QString&       outAbsPath,
                                          QDateTime&     outMtime,
                                          QString&       outErr) const;

        // Cache directory for OBJ conversions of the pulled mesh:
        // Documents/InstaMAT2Remix/MeshCache/<mesh-stem>/. Pure path
        // computation, public for QtTest.
        static QString MeshCacheDirFor(const QString& meshPath);

        // --- InstaMAT2Duplicate helpers (public so QtTest can exercise them
        // without `#define private public` fighting MSVC name mangling on
        // static members — same rationale as the Force Push helpers above). ---

        // 16-character uppercase hex XXH3-64 digest of a fresh random UUID.
        // Used as both the new material's short identity and its Looks prim
        // suffix (/RootNode/Looks/mat_<hash>), matching the existing prim
        // naming convention DeriveDesiredRootFromPrim reads back.
        static QString GenerateMaterialHash();

        // Builds the AperturePBR_Translucency .usda Material definition
        // written alongside each duplicated texture set. channels is a list
        // of (mdlInput, relative texture filename) pairs; diffuse_texture and
        // emissive_mask_texture are annotated colorSpace="sRGB".
        static QString BuildDuplicateUsdaSidecar(
            const QString& materialPrimPath,
            const QList<QPair<QString, QString>>& channels);

        // Builds one POST /ingestcraft/mass-validator/validate request body.
        static QJsonObject BuildDuplicateValidatePayload(
            const QString& jobName,
            const QString& absTexturePath,
            const QString& outDirApi,
            const QString& validationType);

        // Pulls the ingested .dds/.rtex.dds output path out of one entry of
        // completed_schemas' array, preferring a match against origBase and
        // preferring .rtex.dds over .dds.
        static QString ExtractDuplicateIngestedPath(
            const QJsonObject& schema,
            const QString&     origBase);

    private:
        InstaMAT::IInstaMAT& m_instaMAT;
        ExternalTools m_tools;
        mutable Logger m_logger;

        // --- Remix REST helpers ---
        struct RemixSelectionDetails {
            std::string meshFilePath;     // may be absolute or relative
            std::string materialPrimPath; // absolute prim path
            std::string contextFilePath;  // may be empty
        };

        // timeoutSecOverride > 0 forces this call to use that timeout instead of
        // the user's `PollTimeoutSec` setting. Used for long-running endpoints
        // (e.g. /ingestcraft/mass-validator/queue/material) that may take minutes.
        // maxAttemptsOverride > 0 forces that exact total attempt count instead
        // of the default 3-attempt retry; set to 1 for endpoints where retrying
        // would re-queue an in-flight job on the server.
        QJsonDocument RequestJson(const QString& method,
                                  const QString& endpoint,
                                  const QMap<QString, QString>& params,
                                  const QJsonDocument* body,
                                  QString* outError,
                                  double timeoutSecOverride = 0.0,
                                  int    maxAttemptsOverride = 0) const;

        bool GetSelectedRemixAssetDetails(RemixSelectionDetails& outDetails, QString& outError) const;
        bool GetSelectedMaterialPrim(QString& outMaterialPrim, QString& outError) const;
        bool GetRemixDefaultDirectory(QString& outDirAbs, QString& outError) const;
        bool GetMaterialFromMeshPrim(const QString& meshPrim, QString& outMaterialPrim, QString& outError) const;
        bool GetMeshFilePathFromPrim(const QString& prim, QString& outMeshPath, QString& outContextAbs, QString& outError) const;

        // --- Import/Export helpers ---
        QString GetPulledTexturesDir(const QString& remixDefaultDirAbs) const;
        QString DeriveProjectNameFromRemixDir(const QString& remixDefaultDirAbs) const;

        // Walks Remix's /stagecraft/assets/<mat>/textures response and writes each
        // texture into destDir. DDS sources are routed through texconv via
        // m_tools.ConvertDdsToPng. Per-texture failures log + skip (do not abort).
        // progress may be null in tests. Kept private — only PullFromRemix and
        // ImportTexturesFromRemix call it.
        bool DownloadAndConvertTextureList(const QJsonArray& textures,
                                           const QString&    remixDirAbs,
                                           const QString&    destDir,
                                           NamingPolicy      policy,
                                           QProgressDialog*  progress,
                                           QVector<ChannelEntry>& outEntries,
                                           int& outPulledCount,
                                           int& outConvertedCount);

        // Posts one texture to /ingestcraft/mass-validator/queue/material and
        // returns the resolved .rtex.dds output path Remix wrote next to
        // targetIngestDirAbs. pbrType is the canonical channel (albedo, normal,
        // …) used to pick the ingest validation type via kPbrToIngestValidation.
        bool IngestTextureToRemix(const QString& pbrType,
                                  const QString& texturePath,
                                  const QString& targetIngestDirAbs,
                                  QString&       outIngested,
                                  QString&       outIngestErr) const;

        // --- InstaMAT2Duplicate async ingest ------------------------------
        // POST /ingestcraft/mass-validator/validate (max kMaxDuplicateIngestWorkers
        // in flight at once) then poll GET .../completed_schemas until every
        // submitted job resolves or timeoutSeconds elapses. Unlike
        // IngestTextureToRemix (a single blocking call per texture on the
        // synchronous /queue/material endpoint), this never blind-waits: each
        // poll pass only consumes jobs Remix has actually reported complete.
        static constexpr int kMaxDuplicateIngestWorkers = 8;

        // progress may be null; when non-null, its Cancel button is honored
        // at batch-submit boundaries and on each poll pass (a job already
        // POSTed to Remix keeps processing server-side either way — this
        // only stops the plugin waiting on / registering results).
        bool DuplicateIngestChannelsAsync(
            const QHash<QString, QString>& channelFiles, // pbrType -> abs texture path
            const QString&                 targetIngestDirAbs,
            QHash<QString, QString>&       outIngestedPaths, // pbrType -> abs ingested path
            QStringList&                   outErrors,
            QProgressDialog*               progress = nullptr) const;

        // Renders the user's saved layer project into outDir as canonical
        // channel files (albedo.png, normal.png, …). Mirrors WBC's
        // export_project_textures step that runs before Push. Discovery finds
        // the newest non-plugin .IMP in Documents/InstaMAT/Library (with a
        // Ctrl+S Retry dialog when stale); rendering happens OUT OF PROCESS in
        // InstaMAT2RemixExport.exe — executing a layer graph inside Studio
        // fail-fasts on 3.1+ (render-thread GL-context affinity qFatal that no
        // SEH guard can catch). Returns false with an actionable outError;
        // Push then FAILS CLEANLY (no fallback — WBC behavior).
        bool ExportActiveLayeringProject(const QString& outDir,
                                         QStringList&   outChannelFiles,
                                         QString&       outError);

        // Drives Studio's File > Save so Push captures the latest paint before
        // reading the project off disk. Returns false if no Save action is
        // reachable (Push then shows the manual "Ctrl+S then Retry" prompt).
        bool TrySaveActiveProject();

        // Dimensions of the linked material's current Remix texture (first
        // reachable DDS header); invalid QSize when unknown.
        QSize QueryOriginalTextureSize() const;

        // Export resolution policy: Auto (0) → original Remix texture dims;
        // fixed value → aspect-corrected against the original when
        // RestoreAspectOnExport is on; final fallback 2048x2048.
        QSize ComputePushExportSize() const;

        static QString NormalizePathSlashes(const QString& path);
        static QString UrlEncodeKeepSlashes(const QString& value);
        static QString UrlEncodeKeepColonAndSlashes(const QString& value);

        // Cached link state (stored in QSettings on update)
        std::string m_remixApiBaseUrl;
        std::string m_linkedMaterialPrim;
        std::string m_linkedMeshPath;

        // Set by ExportActiveLayeringProject for the Push summary: whether the
        // last export still had 1x1-collapsed channels after all retries, and
        // the resolution the worker actually rendered at (e.g. "4096x4096").
        bool m_exportHadCollapse = false;
        QString m_lastExportSize;
    };
}
