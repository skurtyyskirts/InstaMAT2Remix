import ctypes
import os
import sys
import webbrowser

# -----------------------------------------------------------------------------
# IMPORTANT: This file is installed into InstaMAT Studio's startup scripts folder.
# Many builds will IMPORT this file during app launch. That means:
# - Any unhandled exception here can crash InstaMAT on launch.
# - Importing *another* Qt binding (PySide6) can also destabilize/override the
#   host's Qt runtime. So we must NOT import PySide6 unless explicitly enabled.
# -----------------------------------------------------------------------------

QT_AVAILABLE = False
QMainWindow = None
QApplication = None
QMenu = None
QMessageBox = None


def _ensure_qt_loaded() -> bool:
    """
    Lazily imports PySide6 *only when the Python bridge is enabled*.
    This avoids accidentally pulling in a second/foreign Qt runtime during host startup.
    """
    global QT_AVAILABLE, QMainWindow, QApplication, QMenu, QMessageBox

    if QT_AVAILABLE and QApplication is not None:
        return True

    try:
        from PySide6.QtWidgets import (
            QMainWindow as _QMainWindow,
            QApplication as _QApplication,
            QMenu as _QMenu,
            QMessageBox as _QMessageBox,
        )
        QMainWindow = _QMainWindow
        QApplication = _QApplication
        QMenu = _QMenu
        QMessageBox = _QMessageBox
        QT_AVAILABLE = True
        return True
    except Exception:
        QT_AVAILABLE = False
        return False

try:
    import instamat  # type: ignore
except Exception:
    instamat = None

PLUGIN_DISPLAY_NAME = "RTX Remix"
PLUGIN_ABOUT_TITLE = "RTX Remix Connector for InstaMAT"


def _trace(msg: str) -> None:
    """
    Writes a small startup trace log to %APPDATA%\\InstaMAT2Remix\\startup_trace.log.
    This is purely for debugging load/enable issues.
    """
    try:
        appdata = os.environ.get("APPDATA", "")
        if not appdata:
            return
        root = os.path.join(appdata, "InstaMAT2Remix")
        os.makedirs(root, exist_ok=True)
        path = os.path.join(root, "startup_trace.log")
        with open(path, "a", encoding="utf-8") as f:
            f.write(msg.rstrip() + "\n")
    except Exception:
        pass


def _get_main_window() -> QMainWindow | None:
    if not QT_AVAILABLE or QApplication is None:
        return None
    app = QApplication.instance()
    if not app:
        return None
    for widget in app.topLevelWidgets():
        if isinstance(widget, QMainWindow):
            return widget
    return None


def _add_dll_search_paths(*dirs: str) -> None:
    """
    Python 3.8+ tightened DLL search behavior. Ensure the InstaMAT Studio install
    directory and plugin directory are in the DLL search path so Qt deps resolve.
    """
    add_dir = getattr(os, "add_dll_directory", None)
    if not add_dir:
        return
    for d in dirs:
        try:
            if d and os.path.isdir(d):
                add_dir(d)
        except Exception:
            pass


class RemixConnector:
    def __init__(self, plugin_path: str):
        if instamat is not None:
            instamat.log_info(f"[RTXRemixConnector] Loading DLL: {plugin_path}")
        _trace(f"RemixConnector.__init__: Loading DLL: {plugin_path}")

        # Help Windows find Qt6 DLLs (they live next to the host EXE) and plugin-local deps (texconv).
        exe_dir = os.path.dirname(sys.executable) if sys.executable else ""
        plugin_dir = os.path.dirname(plugin_path)
        _add_dll_search_paths(exe_dir, plugin_dir)

        try:
            self.lib = ctypes.CDLL(plugin_path)

            # Required exports from our C++ DLL.
            for fn in (
                "EnsureInitialized",
                "PullFromRemix",
                "PullFromRemixSelectedMesh",
                "PullFromRemixTilingMesh",
                "ImportTextures",
                "PushToRemix",
                "ForcePushToRemix",
                "OpenSettingsDialog",
                "OpenDiagnosticsDialog",
            ):
                getattr(self.lib, fn)

            # Signatures: all return bool and take no args.
            self.lib.EnsureInitialized.restype = ctypes.c_bool
            self.lib.PullFromRemix.restype = ctypes.c_bool
            self.lib.PullFromRemixSelectedMesh.restype = ctypes.c_bool
            self.lib.PullFromRemixTilingMesh.restype = ctypes.c_bool
            self.lib.ImportTextures.restype = ctypes.c_bool
            self.lib.PushToRemix.restype = ctypes.c_bool
            self.lib.ForcePushToRemix.restype = ctypes.c_bool
            self.lib.OpenSettingsDialog.restype = ctypes.c_bool
            self.lib.OpenDiagnosticsDialog.restype = ctypes.c_bool

            if not bool(self.lib.EnsureInitialized()):
                if instamat is not None:
                    instamat.log_error("[RTXRemixConnector] EnsureInitialized failed (plugin core did not initialize).")
                _trace("EnsureInitialized: FAILED")
            else:
                _trace("EnsureInitialized: OK")

        except Exception as e:
            if instamat is not None:
                instamat.log_error(f"[RTXRemixConnector] Failed to load DLL or resolve exports: {e}")
            _trace(f"RemixConnector.__init__ FAILED: {e}")
            raise

        self.remix_menu: QMenu | None = None

    # --- Menu actions ---

    def _call(self, fn_name: str) -> None:
        try:
            fn = getattr(self.lib, fn_name)
            ok = bool(fn())
            if not ok:
                if QT_AVAILABLE and QMessageBox is not None:
                    QMessageBox.critical(
                        None,
                        "RTX Remix Connector",
                        f"{fn_name} failed. Check InstaMAT/Remix status and logs.",
                    )
        except Exception as e:
            if instamat is not None:
                instamat.log_error(f"[RTXRemixConnector] Exception calling {fn_name}: {e}")
            if QT_AVAILABLE and QMessageBox is not None:
                QMessageBox.critical(None, "RTX Remix Connector", f"Unexpected error: {e}")

    def on_pull_from_remix(self):
        if QT_AVAILABLE and QMessageBox is not None:
            try:
                box = QMessageBox()
                box.setIcon(QMessageBox.Question)
                box.setWindowTitle(PLUGIN_DISPLAY_NAME)
                box.setText(
                    "Pull from RTX Remix and create a new InstaMAT texturing project.\n\n"
                    "Choose how you want to create the project:"
                )
                without_tiling = box.addButton("Without tiling (use selected mesh)", QMessageBox.AcceptRole)
                with_tiling = box.addButton("With tiling (use tiling plane mesh)", QMessageBox.AcceptRole)
                cancel = box.addButton(QMessageBox.Cancel)
                box.setDefaultButton(without_tiling)
                box.exec()
                if box.clickedButton() == cancel:
                    return
                if box.clickedButton() == with_tiling:
                    self._call("PullFromRemixTilingMesh")
                else:
                    self._call("PullFromRemixSelectedMesh")
                return
            except Exception:
                # Fall back to default behavior if the prompt fails.
                pass

        self._call("PullFromRemix")

    def on_import_textures(self):
        self._call("ImportTextures")

    def on_push_to_remix(self):
        self._call("PushToRemix")

    def on_force_push_to_remix(self):
        self._call("ForcePushToRemix")

    def on_open_settings(self):
        self._call("OpenSettingsDialog")

    def on_open_diagnostics(self):
        self._call("OpenDiagnosticsDialog")

    def on_open_documentation(self):
        webbrowser.open("https://github.com/NVIDIAGameWorks/RTX-Remix/tree/main/tools/instamat")

    def on_show_about(self):
        if not QT_AVAILABLE or QMessageBox is None:
            return
        QMessageBox.about(
            None,
            PLUGIN_ABOUT_TITLE,
            "<h2>RTX Remix Connector for InstaMAT</h2>"
            "<p>Bridge between InstaMAT Studio and NVIDIA RTX Remix Toolkit.</p>"
            "<p><b>Menu:</b> RTX Remix</p>",
        )

    def create_menu(self):
        if not QT_AVAILABLE:
            if instamat is not None:
                instamat.log_info("[RTXRemixConnector] PySide6 not available; skipping Python-side menu injection.")
            _trace("create_menu: skipped (QT not available)")
            return
        main_window = _get_main_window()
        if not main_window:
            if instamat is not None:
                instamat.log_error("[RTXRemixConnector] Could not find main window; cannot create menu.")
            return

        # Avoid duplicates if the menu already exists
        for action in main_window.menuBar().actions():
            if action and action.text().strip().lower() == PLUGIN_DISPLAY_NAME.lower():
                self.remix_menu = action.menu()
                break

        if not self.remix_menu:
            self.remix_menu = main_window.menuBar().addMenu(PLUGIN_DISPLAY_NAME)

        self.remix_menu.clear()

        pull_action = self.remix_menu.addAction("Pull from Remix")
        pull_action.triggered.connect(self.on_pull_from_remix)

        import_action = self.remix_menu.addAction("Import Textures from Remix")
        import_action.triggered.connect(self.on_import_textures)

        push_action = self.remix_menu.addAction("Push to Remix")
        push_action.triggered.connect(self.on_push_to_remix)

        force_push_action = self.remix_menu.addAction("Force Push to Remix")
        force_push_action.triggered.connect(self.on_force_push_to_remix)

        self.remix_menu.addSeparator()

        settings_action = self.remix_menu.addAction("Settings...")
        settings_action.triggered.connect(self.on_open_settings)

        diagnostics_action = self.remix_menu.addAction("Diagnostics...")
        diagnostics_action.triggered.connect(self.on_open_diagnostics)

        self.remix_menu.addSeparator()

        doc_action = self.remix_menu.addAction("Documentation")
        doc_action.triggered.connect(self.on_open_documentation)

        about_action = self.remix_menu.addAction("About")
        about_action.triggered.connect(self.on_show_about)

        if instamat is not None:
            instamat.log_info("[RTXRemixConnector] Menu created/updated.")

    def destroy_menu(self):
        if self.remix_menu:
            try:
                # IMPORTANT:
                # The menu is owned by the host menu bar. During shutdown, Qt may already be tearing down
                # widgets; calling deleteLater() here can race with host teardown and cause use-after-free.
                # Best-effort: remove our action from the parent and just clear.
                try:
                    action = self.remix_menu.menuAction()
                    parent = self.remix_menu.parentWidget()
                    if action is not None and parent is not None and hasattr(parent, "removeAction"):
                        parent.removeAction(action)
                except Exception:
                    pass

                self.remix_menu.clear()
            except Exception:
                pass
            self.remix_menu = None


g_connector: RemixConnector | None = None


def on_startup(plugin_path: str | None = None):
    """
    Called by InstaMAT Studio at startup (startup scripts folder).
    """
    global g_connector

    try:
        _trace("on_startup: entered")

        # By default we DO NOT load PySide6 or ctypes-load the C++ DLL from Python at app launch.
        # Reason: loading the plugin DLL here duplicates initialization and can
        # destabilize host startup (missing deps, wrong DLL search order, etc).
        #
        # If you *want* to use the Python bridge, set:
        #   INSTAMAT2REMIX_ENABLE_PYTHON_BRIDGE=1
        if os.environ.get("INSTAMAT2REMIX_ENABLE_PYTHON_BRIDGE", "").strip() != "1":
            _trace("on_startup: Python bridge disabled; leaving initialization to C++ plugin/IMP packaging.")
            if instamat is not None:
                instamat.log_info("[RTXRemixConnector] Startup script loaded (bridge disabled).")
            return

        # Bridge enabled → now it is safe to try importing PySide6.
        if not _ensure_qt_loaded():
            _trace("on_startup: bridge enabled but PySide6 not available; aborting bridge startup.")
            if instamat is not None:
                instamat.log_error("[RTXRemixConnector] Bridge enabled but PySide6 is not available.")
            return

        if plugin_path is None:
            appdata = os.environ.get("APPDATA", "")
            home = os.path.expanduser("~")
            programfiles = os.environ.get("PROGRAMFILES", "C:/Program Files")
            script_dir = os.path.dirname(os.path.abspath(__file__))

            possible_paths = [
                # Preferred: user-local install path (what build_plugin.ps1 writes)
                os.path.join(appdata, "InstaMAT Studio", "Plugins", "InstaMAT2RemixPlugin.dll"),
                os.path.join(appdata, "InstaMAT Studio", "Plugins", "InstaMAT2Remix.dll"),
                # Legacy / older layouts
                os.path.join(home, "Documents", "InstaMAT", "Plugins", "InstaMAT2RemixPlugin.dll"),
                os.path.join(home, "Documents", "InstaMAT", "Plugins", "InstaMAT2Remix.dll"),
                # Dev layouts (repo-relative)
                os.path.join(script_dir, "..", "..", "..", "InstaMAT2Remix", "build", "Release", "InstaMAT2RemixPlugin.dll"),
                os.path.join(script_dir, "..", "..", "..", "InstaMAT2Remix", "build", "Release", "InstaMAT2Remix.dll"),
                # Program Files environment (admin install)
                os.path.join(programfiles, "InstaMAT Studio", "Environment", "InstaMAT2RemixPlugin.dll"),
                os.path.join(programfiles, "InstaMAT Studio", "Environment", "InstaMAT2Remix.dll"),
            ]
        else:
            possible_paths = [plugin_path]

        tried = []
        loaded_path = None

        for candidate in possible_paths:
            candidate = os.path.normpath(candidate)
            if not candidate or not os.path.exists(candidate):
                tried.append(f"{candidate} (missing)")
                continue
            try:
                _trace(f"Trying DLL: {candidate}")
                g_connector = RemixConnector(candidate)
                g_connector.create_menu()
                loaded_path = candidate
                break
            except Exception as e:
                tried.append(f"{candidate} -> {e}")
                _trace(f"Failed DLL: {candidate} -> {e}")

        if not loaded_path:
            if instamat is not None:
                instamat.log_error("[RTXRemixConnector] Failed to load. Tried:\\n" + "\\n".join(tried))
            _trace("on_startup: FAILED (no DLL loaded)")
            return

        if instamat is not None:
            instamat.log_info("=" * 60)
            instamat.log_info("RTX Remix Connector for InstaMAT Studio loaded successfully!")
            instamat.log_info(f"Loaded DLL: {loaded_path}")
            instamat.log_info("Access via menu: RTX Remix")
            instamat.log_info("=" * 60)
        _trace(f"on_startup: OK (loaded {loaded_path})")

    except Exception as e:
        if instamat is not None:
            instamat.log_error(f"[RTXRemixConnector] Startup failed: {e}")
        _trace(f"on_startup: EXCEPTION: {e}")


def on_shutdown():
    global g_connector
    try:
        if g_connector:
            g_connector.destroy_menu()
            g_connector = None
    except Exception as e:
        try:
            if instamat is not None:
                instamat.log_error(f"[RTXRemixConnector] Shutdown error: {e}")
        except Exception:
            pass


# -----------------------------------------------------------------------------
# Auto-run hook
# -----------------------------------------------------------------------------
#
# Some InstaMAT Studio builds execute startup scripts by importing/running the
# file, without calling an explicit on_startup() hook. In that case, the menu
# would never be created.
#
# We keep this idempotent so it is safe even if the host *also* calls
# on_startup() explicitly.
try:
    if g_connector is None:
        on_startup()
except Exception as _e:
    try:
        if instamat is not None:
            instamat.log_error(f"[RTXRemixConnector] Auto-start failed: {_e}")
    except Exception:
        pass


