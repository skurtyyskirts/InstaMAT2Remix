#include "ExternalTools.h"
#include "Utils.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QProcess>
#include <QEventLoop>
#include <QTimer>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace InstaMAT2Remix {

    const char* ExternalTools::s_unwrapScript = R"(
import bpy
import sys
import os

def _parse_args(args):
    angle_limit = 66.0
    island_margin = 0.003
    area_weight = 0.0
    stretch_to_bounds = False

    # Very small manual flag parser to avoid argparse inside Blender runtime.
    i = 0
    while i < len(args):
        a = args[i]
        try:
            if a == '--angle_limit' and i + 1 < len(args):
                angle_limit = float(args[i + 1]); i += 2; continue
            if a == '--island_margin' and i + 1 < len(args):
                island_margin = float(args[i + 1]); i += 2; continue
            if a == '--area_weight' and i + 1 < len(args):
                area_weight = float(args[i + 1]); i += 2; continue
            if a == '--stretch_to_bounds' and i + 1 < len(args):
                v = str(args[i + 1]).strip().lower()
                stretch_to_bounds = v in ['1', 'true', 'yes', 'y', 'on']
                i += 2; continue
        except Exception:
            pass
        i += 1

    return angle_limit, island_margin, area_weight, stretch_to_bounds

def auto_unwrap(input_mesh, output_mesh, angle_limit=66.0, island_margin=0.003, area_weight=0.0, stretch_to_bounds=False):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ext = os.path.splitext(input_mesh)[1].lower()
    if ext == '.obj':
        bpy.ops.wm.obj_import(filepath=input_mesh)
    elif ext in ['.usd', '.usda', '.usdc']:
        bpy.ops.wm.usd_import(filepath=input_mesh)
    elif ext == '.fbx':
        bpy.ops.import_scene.fbx(filepath=input_mesh)
    
    bpy.ops.object.select_all(action='DESELECT')
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            obj.select_set(True)
            bpy.context.view_layer.objects.active = obj

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(
        angle_limit=angle_limit,
        island_margin=island_margin,
        area_weight=area_weight,
        stretch_to_bounds=stretch_to_bounds,
    )
    bpy.ops.object.mode_set(mode='OBJECT')

    out_ext = os.path.splitext(output_mesh)[1].lower()
    if out_ext == '.obj':
        bpy.ops.wm.obj_export(filepath=output_mesh, export_selected_objects=True)
    else:
        bpy.ops.wm.obj_export(filepath=output_mesh + ".obj", export_selected_objects=True)

if __name__ == "__main__":
    argv = sys.argv
    try:
        idx = argv.index("--")
        input_mesh = argv[idx + 1]
        output_mesh = argv[idx + 2]
        extra = argv[idx + 3:]
        angle_limit, island_margin, area_weight, stretch_to_bounds = _parse_args(extra)
        auto_unwrap(input_mesh, output_mesh, angle_limit, island_margin, area_weight, stretch_to_bounds)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
)";

    ExternalTools::ExternalTools(QObject* parent) : QObject(parent) {}

    void ExternalTools::SetBlenderExecutable(const std::string& path) {
        m_blenderPath = path;
    }

    void ExternalTools::SetTexconvExecutable(const std::string& path) {
        m_texconvPath = path;
    }

    std::string ExternalTools::GetBlenderExecutable() const {
        return m_blenderPath;
    }

    std::string ExternalTools::GetTexconvExecutable() const {
        return m_texconvPath;
    }

    bool ExternalTools::RunAutoUnwrap(const std::string& inputMeshPath, std::string& outputMeshPath, const UnwrapParams& params) {
        if (m_blenderPath.empty()) return false;

        QString tempDir = QDir::tempPath();
        QString scriptPath = tempDir + "/remix_unwrap.py";
        
        QFile scriptFile(scriptPath);
        if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&scriptFile);
            out << s_unwrapScript;
            scriptFile.close();
        }

        QFileInfo inputInfo(QString::fromStdString(inputMeshPath));
        QString baseName = inputInfo.baseName();
        outputMeshPath = (tempDir + "/" + baseName + "_unwrapped.obj").toStdString();

        // blender.exe -b -P script.py -- <input> <output>
        QStringList args;
        args << "-b" << "-P" << scriptPath << "--" << QString::fromStdString(inputMeshPath) << QString::fromStdString(outputMeshPath)
             << "--angle_limit" << QString::number(params.angleLimit, 'f', 3)
             << "--island_margin" << QString::number(params.islandMargin, 'f', 6)
             << "--area_weight" << QString::number(params.areaWeight, 'f', 6)
             << "--stretch_to_bounds" << (params.stretchToBounds ? "True" : "False");
        
        return RunProcess(QString::fromStdString(m_blenderPath), args, 30 * 60 * 1000);
    }

    bool ExternalTools::ConvertToDDS(const std::string& inputPngPath, const std::string& outputDdsPath, bool isNormalMap) {
        if (m_texconvPath.empty()) return false;

        const QFileInfo outInfo(QString::fromStdString(outputDdsPath));
        const QString outDir = outInfo.absolutePath();
        const QString desiredBase = outInfo.completeBaseName(); // without extension(s)

        QDir().mkpath(outDir);

        // texconv names output based on input filename, so ensure the input basename matches desired output.
        const QString tempInputPath = outDir + "/" + desiredBase + ".png";
        QFile::remove(tempInputPath);
        if (!QFile::copy(QString::fromStdString(inputPngPath), tempInputPath)) {
            qWarning() << "Failed to stage PNG for texconv:" << tempInputPath;
            return false;
        }

        QString format = isNormalMap ? "BC5_UNORM" : "BC7_UNORM";

        // texconv.exe -ft dds -f <fmt> -y -nologo -o <dir> <input>
        QStringList args;
        args << "-ft" << "dds"
             << "-f" << format
             << "-y" << "-nologo"
             << "-o" << outDir
             << tempInputPath;

        const bool ok = RunProcess(QString::fromStdString(m_texconvPath), args);

        // Cleanup temp staged input
        QFile::remove(tempInputPath);

        // Verify expected output exists
        const QString expectedDds = outDir + "/" + desiredBase + ".dds";
        if (ok && QFileInfo::exists(expectedDds)) {
            return true;
        }

        return false;
    }

    bool ExternalTools::ConvertDdsToPng(const std::string& inputDdsPath, const std::string& outputDir, std::string& outputPngPath) {
        if (m_texconvPath.empty()) return false;
        const QString inPath = QString::fromStdString(inputDdsPath);
        if (!QFileInfo::exists(inPath)) return false;

        const QString outDir = QString::fromStdString(outputDir);
        QDir().mkpath(outDir);

        // texconv replaces only the last extension:
        // "foo.dds" -> "foo.png"
        // "foo.rtex.dds" -> "foo.rtex.png"
        const QString baseName = QFileInfo(inPath).completeBaseName();
        const QString expectedOut = outDir + "/" + baseName + ".png";

        QStringList args;
        args << "-ft" << "png"
             << "-o" << outDir
             << "-y" << "-nologo"
             << inPath;

        const bool ok = RunProcess(QString::fromStdString(m_texconvPath), args);
        if (!ok) return false;

        if (!QFileInfo::exists(expectedOut)) return false;
        outputPngPath = expectedOut.toStdString();
        return true;
    }

    bool ExternalTools::RunProcess(const QString& program, const QStringList& args, int timeoutMs) {
        QProcess process;
        process.setProgram(program);
        process.setArguments(args);
        process.setProcessChannelMode(QProcess::MergedChannels);

#if defined(_WIN32)
        // Best-effort: avoid flashing a console window for CLI tools.
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
            a->flags |= CREATE_NO_WINDOW;
        });
#endif
        
        process.start();
        if (!process.waitForStarted()) {
            qWarning() << "Failed to start process:" << program;
            return false;
        }

        // Keep the UI responsive by running a local event loop.
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        loop.exec();

        if (process.state() != QProcess::NotRunning) {
            qWarning() << "Process timed out:" << program;
            process.kill();
            process.waitForFinished(2000);
            return false;
        }
        
        if (process.exitCode() != 0) {
            qWarning() << "Process failed with exit code:" << process.exitCode();
            qWarning().noquote() << process.readAll();
            return false;
        }
        
        return true;
    }
}
