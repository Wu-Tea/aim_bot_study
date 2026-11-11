"""Reload-only control channel rejects obsolete learning snapshot layouts."""
import ctypes
import unittest
from unittest.mock import patch
from desktop_app.control import ControlChannel, Memory, Snapshot


class ControlProtocolTests(unittest.TestCase):
    def test_reload_snapshot_and_version_boundary(self):
        channel=object.__new__(ControlChannel)
        channel.record={'process_id':123,'process_created':456}
        channel.memory=Memory()
        channel.memory.protocol=5
        channel.memory.snapshot.pid=123
        channel.memory.snapshot.created=456
        self.assertEqual(ctypes.sizeof(Snapshot),576)
        with patch('desktop_app.control.process_identity',return_value={'created':456}):
            result=channel.read()
            self.assertNotIn('regions',result)
            self.assertEqual(result['status'],0)
            for old_version in (1,2,3,4):
                channel.memory.protocol=old_version
                with self.assertRaises(ValueError): channel.read()
