#pragma once
#include "InstaMATAPI.h"
#include "InstaMATPluginAPI.h"
#include "RemixConnector.h"
#include <QPointer>

namespace InstaMAT2Remix {

    class RTXRemixExportNode : public InstaMAT::IInstaMATElementEntityPlugin {
    public:
        RTXRemixExportNode(InstaMAT::IInstaMATPlugin& plugin, RemixConnector* connector);
        virtual ~RTXRemixExportNode() {}
        
        const char* GetName() const override { return "RTX Remix Export"; }
        const char* GetID() const override { return "a2f9b4df-9f7c-4d1d-9a35-52d6b8a5f70a"; }
        const char* GetCategory() const override { return "Remix"; }
        const char* GetDocumentation() const override { return "Exports to RTX Remix."; }
        
        void SetupMetaDataForGraphObject(InstaMAT::IGraphObject& graph) override;
        bool IsExecutionFormatRelevant() const override { return true; }
        
        // Fix types: use InstaMAT::uint32, InstaMAT::IGraph::ParameterType, Definition (inherited from base)
        // Definition is inherited from IInstaMATElementEntityPlugin, so it should be visible inside class scope.
        // But in the declaration it might need qualification or typename.
        bool GetParameterDefinition(const InstaMAT::uint32 index, const InstaMAT::IGraph::ParameterType type, Definition& outParameterDefinition) override;
        
        void Execute(const InstaMAT::IGraph& elementGraph, const InstaMAT::IGraph& entityGraph, InstaMAT::IInstaMATGPUCPUBackend& backend) override;

    private:
        QPointer<RemixConnector> m_connector;
    };

    class RTXRemixImportNode : public InstaMAT::IInstaMATElementEntityPlugin {
    public:
        RTXRemixImportNode(InstaMAT::IInstaMATPlugin& plugin, RemixConnector* connector);
        virtual ~RTXRemixImportNode() {}

        const char* GetName() const override { return "RTX Remix Ingest"; }
        const char* GetID() const override { return "c8a2f2f8-1c8a-4dfd-8c5c-0b26f1b2b8e9"; }
        const char* GetCategory() const override { return "Remix"; }
        const char* GetDocumentation() const override { return "Ingests from RTX Remix Capture."; }

        void SetupMetaDataForGraphObject(InstaMAT::IGraphObject& graph) override;
        bool IsExecutionFormatRelevant() const override { return true; }
        
        bool GetParameterDefinition(const InstaMAT::uint32 index, const InstaMAT::IGraph::ParameterType type, Definition& outParameterDefinition) override;
        
        void Execute(const InstaMAT::IGraph& elementGraph, const InstaMAT::IGraph& entityGraph, InstaMAT::IInstaMATGPUCPUBackend& backend) override;

    private:
        QPointer<RemixConnector> m_connector;
    };
}
