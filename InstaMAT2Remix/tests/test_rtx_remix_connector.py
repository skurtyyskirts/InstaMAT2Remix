import unittest
import sys
import io
from unittest.mock import patch, MagicMock

with patch.dict('sys.modules', {'ctypes': MagicMock()}):
    import InstaMAT2Remix.rtx_remix_connector as rtx_remix_connector


class TestRTXRemixConnector(unittest.TestCase):
    def setUp(self):
        rtx_remix_connector.QT_AVAILABLE = False
        rtx_remix_connector.QApplication = None
        rtx_remix_connector.QMainWindow = None
        rtx_remix_connector.QMenu = None
        rtx_remix_connector.QMessageBox = None

    def test_ensure_qt_loaded_import_error(self):
        # We need to simulate an ImportError when attempting to import PySide6.QtWidgets
        # We do this by mocking builtins.__import__
        original_import = __import__

        def mock_import(name, *args, **kwargs):
            if name == 'PySide6.QtWidgets':
                raise ImportError("No module named PySide6.QtWidgets")
            return original_import(name, *args, **kwargs)

        with patch('builtins.__import__', side_effect=mock_import):
            with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
                result = rtx_remix_connector._ensure_qt_loaded()

                self.assertFalse(result)
                self.assertFalse(rtx_remix_connector.QT_AVAILABLE)
                self.assertIn('[InstaMAT2Remix Python] Could not import PySide6.QtCore.', mock_stdout.getvalue())


class TestPullFromRemix(unittest.TestCase):
    def setUp(self):
        with patch.dict('sys.modules', {'ctypes': MagicMock()}):
            self.connector = rtx_remix_connector.RemixConnector.__new__(rtx_remix_connector.RemixConnector)
            self.connector._call = MagicMock()

    def test_on_pull_from_remix(self):
        """Test on_pull_from_remix calls PullFromRemix."""
        with patch.dict('sys.modules', {'ctypes': MagicMock()}):
            self.connector.on_pull_from_remix()
            self.connector._call.assert_called_once_with("PullFromRemix")


class TestEnsureQtLoaded(unittest.TestCase):
    def setUp(self):
        # Save original globals to restore them later
        self.orig_qt_available = rtx_remix_connector.QT_AVAILABLE
        self.orig_qmain_window = rtx_remix_connector.QMainWindow
        self.orig_qapplication = rtx_remix_connector.QApplication
        self.orig_qmenu = rtx_remix_connector.QMenu
        self.orig_qmessagebox = rtx_remix_connector.QMessageBox

        # Reset globals for testing
        rtx_remix_connector.QT_AVAILABLE = False
        rtx_remix_connector.QMainWindow = None
        rtx_remix_connector.QApplication = None
        rtx_remix_connector.QMenu = None
        rtx_remix_connector.QMessageBox = None

    def tearDown(self):
        # Restore globals
        rtx_remix_connector.QT_AVAILABLE = self.orig_qt_available
        rtx_remix_connector.QMainWindow = self.orig_qmain_window
        rtx_remix_connector.QApplication = self.orig_qapplication
        rtx_remix_connector.QMenu = self.orig_qmenu
        rtx_remix_connector.QMessageBox = self.orig_qmessagebox

    def test_ensure_qt_loaded_success(self):
        """Test that _ensure_qt_loaded successfully imports PySide6 and sets globals."""
        mock_pyside_qtwidgets = MagicMock()
        mock_pyside_qtwidgets.QMainWindow = "MockQMainWindow"
        mock_pyside_qtwidgets.QApplication = "MockQApplication"
        mock_pyside_qtwidgets.QMenu = "MockQMenu"
        mock_pyside_qtwidgets.QMessageBox = "MockQMessageBox"

        with patch.dict('sys.modules', {'PySide6': MagicMock(), 'PySide6.QtCore': MagicMock(), 'PySide6.QtWidgets': mock_pyside_qtwidgets}):
            result = rtx_remix_connector._ensure_qt_loaded()

        self.assertTrue(result)
        self.assertTrue(rtx_remix_connector.QT_AVAILABLE)
        self.assertEqual(rtx_remix_connector.QMainWindow, "MockQMainWindow")
        self.assertEqual(rtx_remix_connector.QApplication, "MockQApplication")
        self.assertEqual(rtx_remix_connector.QMenu, "MockQMenu")
        self.assertEqual(rtx_remix_connector.QMessageBox, "MockQMessageBox")

    def test_ensure_qt_loaded_failure(self):
        """Test that _ensure_qt_loaded handles ImportError and returns False."""
        # Using None in sys.modules simulates an ImportError
        with patch.dict('sys.modules', {'PySide6': None, 'PySide6.QtWidgets': None}):
            result = rtx_remix_connector._ensure_qt_loaded()

        self.assertFalse(result)
        self.assertFalse(rtx_remix_connector.QT_AVAILABLE)
        self.assertIsNone(rtx_remix_connector.QMainWindow)
        self.assertIsNone(rtx_remix_connector.QApplication)
        self.assertIsNone(rtx_remix_connector.QMenu)
        self.assertIsNone(rtx_remix_connector.QMessageBox)

    def test_ensure_qt_loaded_already_loaded(self):
        """Test that _ensure_qt_loaded returns True early if already loaded."""
        rtx_remix_connector.QT_AVAILABLE = True
        rtx_remix_connector.QApplication = "FakeApp"

        # Even if import would fail, it shouldn't be reached
        with patch.dict('sys.modules', {'PySide6': None, 'PySide6.QtWidgets': None}):
            result = rtx_remix_connector._ensure_qt_loaded()

        self.assertTrue(result)
        self.assertTrue(rtx_remix_connector.QT_AVAILABLE)
        self.assertEqual(rtx_remix_connector.QApplication, "FakeApp")


if __name__ == '__main__':
    unittest.main()
