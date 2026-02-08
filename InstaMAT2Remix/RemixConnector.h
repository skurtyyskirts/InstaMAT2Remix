#pragma once
#include "InstaMATAPI.h"
#include "InstaMATPluginAPI.h"
#include "ExternalTools.h"
#include "Logger.h"
#include <string>
#include <vector>
#include <memory>
#include <QObject>
#include <QJsonDocument>
#include <QMap>
#include <QString>

namespace InstaMAT2Remix {
    
    class RemixConnector : public QObject {
        Q_OBJECT
    public:
        enum class PullMeshMode {
            UseSettings = 0,
            SelectedMesh,
            TilingMesh,
        };

        explicit RemixConnector(InstaMAT::IInstaMAT& instaMAT, QObject* parent = nullptr);
        ~RemixConnector();

        void SetRemixApiBaseUrl(const std::string& baseUrl);
        void ReloadSettings();

        QString GetLogFilePath() const;
        bool TestConnection(QString& outMessage) const;
        QString BuildDiagnosticsReport() const;

        // UI actions
        void PullFromRemix(bool autoUnwrap, PullMeshMode meshMode = PullMeshMode::UseSettings);
        void ImportTexturesFromRemix();
        void PushToRemix(bool forceDialog);

        // Template graph runner (PBR template with exposed mesh path input).
        bool RunTemplateGraphForMesh(const QString& meshPath, QString& outMessage, QString& outError);
        
        ExternalTools& GetTools() { return m_tools; }

        static void SetInstance(RemixConnector* instance);
        static RemixConnector* GetInstance();
        
    private:
        InstaMAT::IInstaMAT& m_instaMAT;
        ExternalTools m_tools;
        Logger m_logger;

        // --- Remix REST helpers ---
        struct RemixSelectionDetails {
            std::string meshFilePath;     // may be absolute or relative
            std::string materialPrimPath; // absolute prim path
            std::string contextFilePath;  // may be empty
        };

        QJsonDocument RequestJson(const QString& method,
                                  const QString& endpoint,
                                  const QMap<QString, QString>& params,
                                  const QJsonDocument* body,
                                  QString* outError) const;

        bool GetSelectedRemixAssetDetails(RemixSelectionDetails& outDetails, QString& outError) const;
        bool GetSelectedMaterialPrim(QString& outMaterialPrim, QString& outError) const;
        bool GetRemixDefaultDirectory(QString& outDirAbs, QString& outError) const;
        bool GetMaterialFromMeshPrim(const QString& meshPrim, QString& outMaterialPrim, QString& outError) const;
        bool GetMeshFilePathFromPrim(const QString& prim, QString& outMeshPath, QString& outContextAbs, QString& outError) const;

        // --- Import/Export helpers ---
        QString GetPulledTexturesDir(const QString& remixDefaultDirAbs) const;
        QString DeriveProjectNameFromRemixDir(const QString& remixDefaultDirAbs) const;

        static QString NormalizePathSlashes(const QString& path);
        static QString UrlEncodeKeepSlashes(const QString& value);
        static QString UrlEncodeKeepColonAndSlashes(const QString& value);

        // Cached link state (stored in QSettings on update)
        std::string m_remixApiBaseUrl;
        std::string m_linkedMaterialPrim;
        std::string m_linkedMeshPath;
    };
}
