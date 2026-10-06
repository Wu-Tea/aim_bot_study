"""Run real Tk checks on a private Windows desktop, without switching desktops.

The launcher owns the desktop until its child exits. Isolation failures are
reported; tests never silently fall back onto the interactive user desktop.
"""
import ctypes
from ctypes import wintypes as w
import os
from pathlib import Path
import subprocess
import sys
import uuid
import codecs


class StartupInfo(ctypes.Structure):
    _fields_ = [('cb', w.DWORD), ('reserved', w.LPWSTR), ('desktop', w.LPWSTR), ('title', w.LPWSTR),
        ('x', w.DWORD), ('y', w.DWORD), ('width', w.DWORD), ('height', w.DWORD),
        ('chars_x', w.DWORD), ('chars_y', w.DWORD), ('fill', w.DWORD), ('flags', w.DWORD),
        ('show', w.WORD), ('reserved_size', w.WORD), ('reserved_bytes', ctypes.c_void_p),
        ('stdin', w.HANDLE), ('stdout', w.HANDLE), ('stderr', w.HANDLE)]


class ProcessInfo(ctypes.Structure):
    _fields_ = [('process', w.HANDLE), ('thread', w.HANDLE), ('pid', w.DWORD), ('tid', w.DWORD)]


class SecurityAttributes(ctypes.Structure):
    _fields_ = [('size', w.DWORD), ('descriptor', ctypes.c_void_p), ('inherit', w.BOOL)]


def desktop_name():
    user = ctypes.WinDLL('user32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    user.GetThreadDesktop.argtypes = [w.DWORD]
    user.GetThreadDesktop.restype = w.HANDLE
    user.GetUserObjectInformationW.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD)]
    text, size = ctypes.create_unicode_buffer(256), w.DWORD()
    if not user.GetUserObjectInformationW(user.GetThreadDesktop(kernel.GetCurrentThreadId()), 2,
                                         text, ctypes.sizeof(text), ctypes.byref(size)):
        raise ctypes.WinError(ctypes.get_last_error())
    return text.value


def require_isolated_gui():
    expected = os.environ.get('AIM_GUI_TEST_DESKTOP')
    if not expected or not expected.startswith('AimGuiTest_') or desktop_name() != expected:
        raise RuntimeError('Run GUI checks with: python -m desktop_app.test_desktop; interactive desktop tests are disabled.')


def run_isolated(arguments, *, cwd=None):
    if sys.platform != 'win32':
        raise OSError('GUI isolation requires Windows.')
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, ctypes.c_void_p, w.DWORD, w.DWORD, ctypes.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.CloseDesktop.restype = w.BOOL
    name = 'AimGuiTest_' + uuid.uuid4().hex
    # Window/menu creation, object read/write and enumeration; no switch right.
    handle = user.CreateDesktopW(name, None, None, 0, 0x0001 | 0x0002 | 0x0004 | 0x0040 | 0x0080, None)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        # CPython subprocess.STARTUPINFO does not marshal lpDesktop. Use the
        # real Win32 structure rather than setting an ignored Python attribute.
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreatePipe.argtypes = [ctypes.POINTER(w.HANDLE), ctypes.POINTER(w.HANDLE), ctypes.POINTER(SecurityAttributes), w.DWORD]
        kernel.SetHandleInformation.argtypes = [w.HANDLE, w.DWORD, w.DWORD]
        kernel.CloseHandle.argtypes = [w.HANDLE]
        kernel.ReadFile.argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.c_void_p]
        kernel.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, w.BOOL,
            w.DWORD, ctypes.c_void_p, w.LPCWSTR, ctypes.POINTER(StartupInfo), ctypes.POINTER(ProcessInfo)]
        kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
        kernel.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
        kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
        reader, writer = w.HANDLE(), w.HANDLE()
        security = SecurityAttributes(ctypes.sizeof(SecurityAttributes), None, True)
        if not kernel.CreatePipe(ctypes.byref(reader), ctypes.byref(writer), ctypes.byref(security), 0):
            raise ctypes.WinError(ctypes.get_last_error())
        process = ProcessInfo()
        environment = os.environ.copy()
        environment['AIM_GUI_TEST_DESKTOP'] = name
        environment['PYTHONIOENCODING'] = 'utf-8'
        environment['PYTHONUNBUFFERED'] = '1'
        block = ctypes.create_unicode_buffer('\0'.join(f'{k}={v}' for k,v in sorted(environment.items())) + '\0\0')
        startup = StartupInfo()
        startup.cb = ctypes.sizeof(startup)
        startup.desktop = name
        startup.flags = 0x00000100 | 0x00000080  # std handles + no busy cursor
        startup.stdout = startup.stderr = writer
        try:
            if not kernel.SetHandleInformation(reader, 1, 0):
                raise ctypes.WinError(ctypes.get_last_error())
            command = ctypes.create_unicode_buffer(subprocess.list2cmdline([sys.executable, *arguments]))
            if not kernel.CreateProcessW(sys.executable, command, None, None, True,
                0x08000000 | 0x00000400, block, str(cwd) if cwd else None, ctypes.byref(startup), ctypes.byref(process)):
                raise ctypes.WinError(ctypes.get_last_error())
            kernel.CloseHandle(writer); writer = w.HANDLE()
            decoder = codecs.getincrementaldecoder('utf-8')()
            buffer, size = ctypes.create_string_buffer(4096), w.DWORD()
            while kernel.ReadFile(reader, buffer, len(buffer), ctypes.byref(size), None) and size.value:
                sys.stdout.write(decoder.decode(buffer.raw[:size.value])); sys.stdout.flush()
            if ctypes.get_last_error() not in (0, 109):  # ERROR_BROKEN_PIPE = normal EOF
                raise ctypes.WinError(ctypes.get_last_error())
            sys.stdout.write(decoder.decode(b'', final=True))
            kernel.WaitForSingleObject(process.process, 0xffffffff)
            result = w.DWORD()
            if not kernel.GetExitCodeProcess(process.process, ctypes.byref(result)):
                raise ctypes.WinError(ctypes.get_last_error())
            return result.value
        except BaseException:
            if process.process:
                kernel.TerminateProcess(process.process, 1)
                kernel.WaitForSingleObject(process.process, 5000)
            raise
        finally:
            for owned in (writer, reader, process.thread, process.process):
                if owned:kernel.CloseHandle(owned)
    finally:
        if not user.CloseDesktop(handle):
            raise ctypes.WinError(ctypes.get_last_error())


def main():
    root = Path(__file__).resolve().parents[2]
    arguments = sys.argv[1:] or ['-m', 'unittest', 'discover', '-s', 'python/tests', '-p', 'test_desktop*.py']
    raise SystemExit(run_isolated(arguments, cwd=root))


if __name__ == '__main__':
    main()
