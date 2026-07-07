#include "InstaMATAPI.h"
#include "InstaMATPluginAPI.h"
#include "RemixConnector.h"
#include "GuiManager.h"
#include "RemixNodes.h"
#include <iostream>
#include <memory>
#if defined(_WIN32)
#include <windows.h>
#endif

class InstaMAT2RemixPlugin : public InstaMAT::IInstaMATPlugin {
public:
    InstaMAT2RemixPlugin(InstaMAT::IInstaMAT& instaMAT, InstaMAT::IInstaMATObjectManager& objectManager,
                         InstaMAT::int32 hostVersion)
        : InstaMAT::IInstaMATPlugin(instaMAT, objectManager), m_hostVersion(hostVersion) {
    }

    virtual ~InstaMAT2RemixPlugin() {}

    // Report the host's own API version. The bundled SDK headers can trail the
    // installed Studio (e.g. headers are v3.0 but Studio is v3.4), so echoing the
    // host version keeps a compatible same-major host from rejecting us if it
    // re-checks the version after load.
    InstaMAT::int32 GetVersion() const override {
        return m_hostVersion;
    }

    void Initialize() override {
        try {
            // Initialize Core Logic (keep constructor light; do not hard-fail host).
            m_connector = std::make_unique<InstaMAT2Remix::RemixConnector>(InstaMAT);
            InstaMAT2Remix::RemixConnector::SetInstance(m_connector.get());

            // Register custom nodes (ElementEntity plugins) so they show up in the library.
            m_exportNode = std::make_unique<InstaMAT2Remix::RTXRemixExportNode>(*this, m_connector.get());
            m_importNode = std::make_unique<InstaMAT2Remix::RTXRemixImportNode>(*this, m_connector.get());
            InstaMAT.RegisterElementEntityPlugin(*m_exportNode);
            InstaMAT.RegisterElementEntityPlugin(*m_importNode);

            // Initialize UI (fails soft if Qt isn't ready; see GuiManager guards).
            m_gui = std::make_unique<InstaMAT2Remix::GuiManager>(m_connector.get());
            m_gui->Initialize();

            std::cout << "[InstaMAT2Remix] Initialized." << std::endl;
        } catch (const std::exception& e) {
            // Fail-soft: do not take down the host on plugin init failure.
            std::cerr << "[InstaMAT2Remix] Initialize failed: " << e.what() << std::endl;
            // Ensure we don't leave partially registered nodes behind.
            Shutdown();
        } catch (...) {
            std::cerr << "[InstaMAT2Remix] Initialize failed: unknown error" << std::endl;
            Shutdown();
        }
    }

    void Shutdown() override {
        InstaMAT2Remix::RemixConnector::SetInstance(nullptr);
        if (m_exportNode) {
            InstaMAT.UnregisterElementEntityPlugin(*m_exportNode);
        }
        if (m_importNode) {
            InstaMAT.UnregisterElementEntityPlugin(*m_importNode);
        }
        m_exportNode.reset();
        m_importNode.reset();
        if (m_gui) {
            m_gui->Teardown();
            m_gui.reset();
        }
        m_connector.reset();
    }

    void Dealloc() override {
        delete this;
    }

private:
    InstaMAT::int32 m_hostVersion;
    std::unique_ptr<InstaMAT2Remix::RemixConnector> m_connector;
    std::unique_ptr<InstaMAT2Remix::GuiManager> m_gui;
    std::unique_ptr<InstaMAT2Remix::RTXRemixExportNode> m_exportNode;
    std::unique_ptr<InstaMAT2Remix::RTXRemixImportNode> m_importNode;
};

INSTAMATPLUGIN_API InstaMAT::uint32 GetInstaMATPlugin(const InstaMAT::int32 version,
                                                      InstaMAT::IInstaMAT& instaMAT,
                                                      InstaMAT::IInstaMATObjectManager& objectManager,
                                                      InstaMAT::IInstaMATPlugin** outInstaMATPlugin)
{
    // Accept any host with the same MAJOR API version. Minor SDK bumps are
    // additive and backward-compatible, and the bundled SDK headers can trail
    // the installed Studio (headers here are v3.0; Studio may be v3.4+), so an
    // exact-equality check would wrongly reject a compatible host and the plugin
    // would silently fail to register ("plugin reported wrong version").
    if ((version >> 16) != (INSTAMATPLUGIN_API_VERSION >> 16))
        return INSTAMATPLUGIN_RETURNVALUE_INVALIDVERSION;
    if (outInstaMATPlugin == nullptr) return INSTAMATPLUGIN_RETURNVALUE_FAILED;

    *outInstaMATPlugin = new InstaMAT2RemixPlugin(instaMAT, objectManager, version);
    return INSTAMATPLUGIN_RETURNVALUE_SUCCESS;
}

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
#endif
