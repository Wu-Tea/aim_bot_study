"""Two physical camera models; old four-region channels must not be misread."""
import ctypes
import unittest
from unittest.mock import patch
from desktop_app.control import ControlChannel, Memory, Snapshot


class PhysicalCameraProtocolTests(unittest.TestCase):
    def test_two_camera_snapshot_and_version_boundary(self):
        channel=object.__new__(ControlChannel)
        channel.record={'process_id':123,'process_created':456}
        channel.memory=Memory()
        channel.memory.protocol=2
        channel.memory.snapshot.pid=123
        channel.memory.snapshot.created=456
        channel.memory.snapshot.regions[0].effective=1700
        channel.memory.snapshot.regions[1].effective=2500
        self.assertEqual(ctypes.sizeof(Snapshot),608)
        with patch('desktop_app.control.process_identity',return_value={'created':456}):
            result=channel.read()
            self.assertEqual([v['effective'] for v in result['regions']],[1700,2500])
            channel.memory.protocol=1
            with self.assertRaises(ValueError): channel.read()
