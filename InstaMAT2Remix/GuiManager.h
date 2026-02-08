#pragma once
#include <QObject>
#include <QMenu>
#include <QMenuBar>
#include <QMainWindow>
#include <QAction>
#include <QSettings>
#include <QApplication>
#include <QPointer>

namespace InstaMAT2Remix {
    class RemixConnector;

    class GuiManager : public QObject {
        Q_OBJECT
    public:
        explicit GuiManager(RemixConnector* connector, QObject* parent = nullptr);
        ~GuiManager();

        void Initialize();
        void Teardown();

    private slots:
        void onPullFromRemix();
        void onImportTexturesFromRemix();
        void onPushToRemix();
        void onForcePushToRemix();
        void onSettings();
        void onDiagnostics();
        void onAbout();

    private:
        QPointer<RemixConnector> m_connector;
        // QPointer auto-nulls when the QObject is deleted by Qt during app shutdown,
        // preventing use-after-free crashes.
        QPointer<QMenu> m_remixMenu;
        
        void InjectMenu();
        QMainWindow* GetMainWindow();
    };
}

