import unittest
from unittest.mock import MagicMock, patch
import os
import sys

# Add the directory containing the module to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

class TestRemixConnector(unittest.TestCase):
    def setUp(self):
        # We need to mock out ctypes and other things before importing
        with patch.dict('sys.modules', {'ctypes': MagicMock()}):
            import InstaMAT2Remix.rtx_remix_connector as connector
            # Create a mock connector without initializing the C++ plugin
            self.connector = connector.RemixConnector.__new__(connector.RemixConnector)
            self.connector._call = MagicMock()

    def test_on_pull_from_remix(self):
        """Test on_pull_from_remix calls PullFromRemix."""
        # Use patch.dict to avoid global sys.modules pollution
        with patch.dict('sys.modules', {'ctypes': MagicMock()}):
            self.connector.on_pull_from_remix()
            self.connector._call.assert_called_once_with("PullFromRemix")

if __name__ == '__main__':
    unittest.main()
