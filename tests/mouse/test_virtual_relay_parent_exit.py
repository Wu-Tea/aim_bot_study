"""A real parent-process death must not leave the simulated worker alive."""
import ctypes
import os
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'artifacts/mouse_link/virtual-build/Release/mouse_virtual_relay.exe'


@unittest.skipUnless(os.name == 'nt' and EXE.is_file(), 'Windows relay build required')
class ParentExitTests(unittest.TestCase):
    def test_worker_exits_after_parent_is_terminated(self):
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel.WaitForSingleObject.restype = ctypes.c_uint32
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / 'session'
            parent = subprocess.Popen([str(EXE), '--smoke', '--out', str(directory)],
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      creationflags=subprocess.CREATE_NO_WINDOW)
            ready = queue.Queue()
            def read():
                for line in parent.stdout:
                    if line.startswith(b'READY:'):
                        ready.put(True)
                        return
                ready.put(False)
            thread = threading.Thread(target=read, daemon=True)
            thread.start()
            worker = None
            try:
                self.assertTrue(ready.get(timeout=4), 'Parent/worker did not become ready')
                worker_id = int((directory / 'worker-pid.txt').read_text())
                worker = kernel.OpenProcess(0x00100000, False, worker_id)
                self.assertTrue(worker)
                self.assertEqual(kernel.WaitForSingleObject(worker, 0), 258)
                parent.terminate()
                parent.wait(timeout=3)
                self.assertEqual(kernel.WaitForSingleObject(worker, 1500), 0,
                                 'Worker outlived the dead parent')
            finally:
                if parent.poll() is None:
                    parent.terminate()
                    parent.wait(timeout=3)
                if worker:
                    kernel.CloseHandle(worker)
                thread.join(timeout=1)
                parent.stdout.close()


if __name__ == '__main__':
    unittest.main()
