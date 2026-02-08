#pragma once
#include <string>
#include <QObject>
#include <QProcess>

namespace InstaMAT2Remix {

    class ExternalTools : public QObject {
        Q_OBJECT
    public:
        explicit ExternalTools(QObject* parent = nullptr);

        struct UnwrapParams {
            double angleLimit = 66.0;
            double islandMargin = 0.003;
            double areaWeight = 0.0;
            bool stretchToBounds = false;
        };
        
        // Configure paths
        void SetBlenderExecutable(const std::string& path);
        void SetTexconvExecutable(const std::string& path);
        
        std::string GetBlenderExecutable() const;
        std::string GetTexconvExecutable() const;

        // Actions
        bool RunAutoUnwrap(const std::string& inputMeshPath, std::string& outputMeshPath, const UnwrapParams& params);
        bool ConvertToDDS(const std::string& inputPngPath, const std::string& outputDdsPath, bool isNormalMap);
        bool ConvertDdsToPng(const std::string& inputDdsPath, const std::string& outputDir, std::string& outputPngPath);

    private:
        std::string m_blenderPath;
        std::string m_texconvPath;
        
        // Embedded Python script for Blender
        static const char* s_unwrapScript;
        
        bool RunProcess(const QString& program, const QStringList& args, int timeoutMs = 15 * 60 * 1000);
    };
}
