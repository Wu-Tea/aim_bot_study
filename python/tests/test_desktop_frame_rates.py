import ctypes
from ctypes import wintypes as w
from contextlib import ExitStack
import os
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

from desktop_app.frame_rates import FrameMemory, read_frame_rates, rate
from desktop_app.observation import RuntimeObserver
from desktop_app.runtime import RuntimeManager, kernel, process_identity


class FrameRateMemoryTests(unittest.TestCase):
    """Use actual Win32 RAM mappings, with filesystem APIs forbidden to readers."""
    def setUp(self):
        self.pid = os.getpid()
        self.record = {'process_id': self.pid, 'process_created': process_identity(self.pid)['created']}
        kernel.CreateFileMappingW.argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD, w.DWORD, w.DWORD, w.LPCWSTR]
        kernel.CreateFileMappingW.restype = w.HANDLE
        self.handle = kernel.CreateFileMappingW(ctypes.c_void_p(-1), None, 4, 0,
            ctypes.sizeof(FrameMemory), f'Local\\cod_native_fps_{self.pid}')
        self.assertTrue(self.handle)
        self.pointer = kernel.MapViewOfFile(self.handle, 0xF001F, 0, 0, ctypes.sizeof(FrameMemory))
        self.assertTrue(self.pointer)
        ctypes.memset(self.pointer, 0, ctypes.sizeof(FrameMemory))
        self.memory = FrameMemory.from_address(self.pointer)
        self.memory.protocol = 1
        self.memory.sequence = 2
        snapshot = self.memory.snapshot
        snapshot.pid, snapshot.created, snapshot.state = self.pid, self.record['process_created'], 1
        snapshot.sampled_at_ms = 12345
        values = {'elapsed_ns': 10_000_000_000, 'aim_ns': 5_000_000_000,
            'vision_frames': 1120, 'aim_frames': 820, 'controller_ticks': 5000,
            'recent_elapsed_ns': 5_000_000_000, 'recent_aim_ns': 4_000_000_000,
            'recent_vision_frames': 640, 'recent_aim_frames': 640, 'aiming': 1}
        for name, value in values.items(): setattr(snapshot.counts, name, value)

    def tearDown(self):
        kernel.UnmapViewOfFile(self.pointer)
        kernel.CloseHandle(self.handle)

    def test_real_shared_memory_reader_and_manager_do_not_access_files_or_logs(self):
        manager = RuntimeManager(Path.cwd())
        with ExitStack() as stack:
            for name in ('builtins.open', 'pathlib.Path.open', 'pathlib.Path.read_text',
                         'pathlib.Path.read_bytes', 'pathlib.Path.write_text', 'pathlib.Path.write_bytes'):
                stack.enter_context(patch(name, side_effect=AssertionError('FPS must use RAM only')))
            stack.enter_context(patch.object(manager, 'active', side_effect=AssertionError('reuse observer identity')))
            value = manager.frame_rates(self.record)
        self.assertEqual(value['aim_frames'], 820)
        self.assertEqual(value['pid'], self.pid)
        self.assertEqual(value['sampled_at_ms'], 12345)
        self.assertEqual(rate(value['aim_frames'], value['aim_ns']), 164)
        self.assertEqual(rate(value['recent_aim_frames'], value['recent_aim_ns']), 160)

    def test_pid_reuse_does_not_read_another_session(self):
        wrong = dict(self.record, process_created=self.record['process_created'] + 1)
        with patch.object(kernel, 'OpenFileMappingW') as open_mapping:
            self.assertIsNone(read_frame_rates(wrong))
        open_mapping.assert_not_called()

    def test_in_progress_publication_returns_no_torn_sample(self):
        self.memory.sequence = 3
        self.assertIsNone(read_frame_rates(self.record))
        self.memory.sequence = 4
        self.assertEqual(read_frame_rates(self.record)['aim_frames'], 820)

    def test_protocol_identity_and_invalid_counts_are_rejected(self):
        cases = [(self.memory, 'protocol', 2), (self.memory.snapshot, 'created', 0),
                 (self.memory.snapshot, 'state', 9), (self.memory.snapshot.counts, 'aim_ns', 11_000_000_000),
                 (self.memory.snapshot.counts, 'aim_frames', 1121),
                 (self.memory.snapshot.counts, 'recent_elapsed_ns', 5_000_000_001),
                 (self.memory.snapshot.counts, 'recent_aim_ns', 5_000_000_001)]
        for owner, name, bad in cases:
            with self.subTest(name=name):
                original = getattr(owner, name)
                setattr(owner, name, bad)
                try:
                    with self.assertRaises(ValueError): read_frame_rates(self.record)
                finally: setattr(owner, name, original)

    def test_reader_closes_its_view_and_handle_after_success_and_error(self):
        with patch.object(kernel, 'UnmapViewOfFile', wraps=kernel.UnmapViewOfFile) as unmap, \
             patch.object(kernel, 'CloseHandle', wraps=kernel.CloseHandle) as close:
            read_frame_rates(self.record)
            self.memory.protocol = 2
            with self.assertRaises(ValueError): read_frame_rates(self.record)
        self.assertEqual(unmap.call_count, 2)
        # Includes the temporary process identity handles.
        self.assertEqual(close.call_count, 4)

    def test_missing_channel_from_older_runtime_is_an_empty_state(self):
        with patch.object(kernel, 'OpenFileMappingW', return_value=None):
            self.assertIsNone(read_frame_rates(self.record))


class FrameRateObserverTests(unittest.TestCase):
    def test_observer_passes_existing_running_identity_and_reads_off_ui_thread(self):
        record = {'process_id': 42, 'process_created': 77}
        manager = Mock()
        manager.status.return_value = {'phase': 'running', 'record': record}
        manager.frame_rates.return_value = {'aim_frames': 820}
        observer = RuntimeObserver(manager)
        try:
            observer.request(performance=True)
            result = observer.results.get(timeout=2)
            self.assertEqual(result['performance'], {'aim_frames': 820})
            manager.frame_rates.assert_called_once_with(record)
            manager.learning.assert_not_called()
            manager.fusion_state.assert_not_called()
            manager.active.assert_not_called()
        finally:
            observer.close()
            observer.thread.join(timeout=2)

    def test_zero_aim_time_is_empty_and_zero_frames_during_aim_are_zero_fps(self):
        self.assertIsNone(rate(0, 0))
        self.assertEqual(rate(0, 5_000_000_000), 0)


if __name__ == '__main__': unittest.main()
