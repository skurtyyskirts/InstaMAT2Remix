#include "RemixNodes.h"
#include <cstring>
#include <QCoreApplication>
#include <QMetaObject>

namespace InstaMAT2Remix {

    using namespace InstaMAT;

    // --- Export Node ---

    RTXRemixExportNode::RTXRemixExportNode(IInstaMATPlugin& plugin, RemixConnector* connector)
        : IInstaMATElementEntityPlugin(plugin), m_connector(connector) {}

    void RTXRemixExportNode::SetupMetaDataForGraphObject(IGraphObject& graph) {
        graph.SetMetaDataAsChar(MetaData::KeyAuthor, "InstaMAT2Remix");
        graph.SetMetaDataAsChar(MetaData::KeyCategory, "Remix");
    }

    bool RTXRemixExportNode::GetParameterDefinition(const uint32 index, const IGraph::ParameterType type, Definition& def) {
        if (type == IGraph::ParameterTypeInput) {
            switch(index) {
                case 0: def.Name="Trigger Export"; def.Type=IGraphVariable::TypeBoolean; def.ArithmeticValue.BooleanValue=false; return true;
                case 1: def.Name="Asset Name"; def.Type=IGraphVariable::TypeElementString; def.StringValue="MyAsset"; return true;
                case 2: def.Name="Albedo"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 3: def.Name="Normal"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 4: def.Name="Roughness"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 5: def.Name="Metallic"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 6: def.Name="Emissive"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 7: def.Name="Height"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 8: def.Name="Opacity"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 9: def.Name="Ambient Occlusion"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 10: def.Name="Transmittance"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 11: def.Name="IOR"; def.Type=IGraphVariable::TypeElementImage; return true;
                case 12: def.Name="Subsurface"; def.Type=IGraphVariable::TypeElementImage; return true;
            }
        }
        return false;
    }

    void RTXRemixExportNode::Execute(const IGraph& elementGraph, const IGraph& entityGraph, IInstaMATGPUCPUBackend& backend) {
        ArithmeticGraphValue trigger;
        if (backend.GetInputParameterConstantValue("Trigger Export", &trigger) && trigger.BooleanValue) {
            // NOTE: ElementEntity plugins may execute on a non-UI thread depending on host settings.
            // Keep this node as a simple trigger that forwards to the connector.
            static bool exporting = false;
            if (exporting) return;
            exporting = true;

            if (m_connector) {
                QPointer<RemixConnector> connectorGuard = m_connector;
                // Push to currently linked Remix material (marshal to UI thread).
                QCoreApplication* app = QCoreApplication::instance();
                if (app) {
                    QMetaObject::invokeMethod(
                        app,
                        [connectorGuard]() {
                            if (connectorGuard) connectorGuard->PushToRemix(false);
                        },
                        Qt::QueuedConnection);
                }
            }

            exporting = false;
        }
    }

    // --- Import Node ---

    RTXRemixImportNode::RTXRemixImportNode(IInstaMATPlugin& plugin, RemixConnector* connector)
        : IInstaMATElementEntityPlugin(plugin), m_connector(connector) {}

    void RTXRemixImportNode::SetupMetaDataForGraphObject(IGraphObject& graph) {
        graph.SetMetaDataAsChar(MetaData::KeyAuthor, "InstaMAT2Remix");
        graph.SetMetaDataAsChar(MetaData::KeyCategory, "Remix");
    }

    bool RTXRemixImportNode::GetParameterDefinition(const uint32 index, const IGraph::ParameterType type, Definition& def) {
        if (type == IGraph::ParameterTypeInput) {
             switch(index) {
                 case 0: def.Name="Pull Selected Mesh"; def.Type=IGraphVariable::TypeBoolean; def.ArithmeticValue.BooleanValue=false; return true;
                 case 1: def.Name="Pull Tiling Mesh"; def.Type=IGraphVariable::TypeBoolean; def.ArithmeticValue.BooleanValue=false; return true;
                 case 2: def.Name="Import Textures"; def.Type=IGraphVariable::TypeBoolean; def.ArithmeticValue.BooleanValue=false; return true;
             }
        }
        return false;
    }

    void RTXRemixImportNode::Execute(const IGraph& elementGraph, const IGraph& entityGraph, IInstaMATGPUCPUBackend& backend) {
        ArithmeticGraphValue triggerPull;
        if (backend.GetInputParameterConstantValue("Pull Selected Mesh", &triggerPull) && triggerPull.BooleanValue) {
            static bool pulling = false;
            if (pulling) return;
            pulling = true;

            if (m_connector) {
                QPointer<RemixConnector> connectorGuard = m_connector;
                // Pull mesh and setup project (marshal to UI thread).
                QCoreApplication* app = QCoreApplication::instance();
                if (app) {
                    QMetaObject::invokeMethod(
                        app,
                        [connectorGuard]() {
                            if (connectorGuard) connectorGuard->PullFromRemix(true, RemixConnector::PullMeshMode::SelectedMesh);
                        },
                        Qt::QueuedConnection);
                }
            }
            pulling = false;
        }

        ArithmeticGraphValue triggerPullTiling;
        if (backend.GetInputParameterConstantValue("Pull Tiling Mesh", &triggerPullTiling) && triggerPullTiling.BooleanValue) {
            static bool pullingTiling = false;
            if (pullingTiling) return;
            pullingTiling = true;

            if (m_connector) {
                QPointer<RemixConnector> connectorGuard = m_connector;
                // Pull tiling mesh and setup project (marshal to UI thread).
                QCoreApplication* app = QCoreApplication::instance();
                if (app) {
                    QMetaObject::invokeMethod(
                        app,
                        [connectorGuard]() {
                            if (connectorGuard) connectorGuard->PullFromRemix(true, RemixConnector::PullMeshMode::TilingMesh);
                        },
                        Qt::QueuedConnection);
                }
            }
            pullingTiling = false;
        }

        ArithmeticGraphValue triggerImport;
        if (backend.GetInputParameterConstantValue("Import Textures", &triggerImport) && triggerImport.BooleanValue) {
            static bool importing = false;
            if (importing) return;
            importing = true;

            if (m_connector) {
                QPointer<RemixConnector> connectorGuard = m_connector;
                // Pull textures from the currently selected/linked Remix asset (marshal to UI thread).
                QCoreApplication* app = QCoreApplication::instance();
                if (app) {
                    QMetaObject::invokeMethod(
                        app,
                        [connectorGuard]() {
                            if (connectorGuard) connectorGuard->ImportTexturesFromRemix();
                        },
                        Qt::QueuedConnection);
                }
            }

            importing = false;
        }
    }
}
