"""Versioned, local native control channel; no gameplay input access."""
import ctypes
from ctypes import wintypes as w
import time
import uuid

from .runtime import kernel, process_identity


class Region(ctypes.Structure):
    _fields_ = [('effective', ctypes.c_float), ('learned', ctypes.c_float),
                ('confidence', ctypes.c_float), ('samples', w.DWORD)]


class Snapshot(ctypes.Structure):
    _fields_ = [('pid', w.DWORD), ('status', w.DWORD), ('created', ctypes.c_uint64),
                ('request_id', ctypes.c_uint64), ('completed_id', ctypes.c_uint64),
                ('revision', ctypes.c_uint64), ('sampled_at_ms', ctypes.c_uint64),
                ('regions', Region * 4), ('fire_output', ctypes.c_char * 8),
                ('manual_fire_input', ctypes.c_char * 8), ('message', ctypes.c_char * 512)]


class Memory(ctypes.Structure):
    _fields_ = [('sequence', w.LONG), ('protocol', w.DWORD), ('requested_id', ctypes.c_uint64), ('snapshot', Snapshot)]


assert ctypes.sizeof(Snapshot) == 640 and Memory.snapshot.offset == 16
kernel.OpenFileMappingW.argtypes = [w.DWORD, w.BOOL, w.LPCWSTR]
kernel.OpenFileMappingW.restype = w.HANDLE
kernel.MapViewOfFile.argtypes = [w.HANDLE, w.DWORD, w.DWORD, w.DWORD, ctypes.c_size_t]
kernel.MapViewOfFile.restype = ctypes.c_void_p
kernel.UnmapViewOfFile.argtypes = [ctypes.c_void_p]


class ControlChannel:
    def __init__(self, record):
        self.record = record
        self.handle = kernel.OpenFileMappingW(6, False, f'Local\\cod_native_control_{record["process_id"]}')
        if not self.handle:
            raise ValueError('当前程序未提供热加载/学习通道，请停止后启动更新版本。')
        self.pointer = kernel.MapViewOfFile(self.handle, 6, 0, 0, ctypes.sizeof(Memory))
        if not self.pointer:
            kernel.CloseHandle(self.handle)
            raise OSError('无法读取原生配置通道')
        self.memory = Memory.from_address(self.pointer)

    def close(self):
        kernel.UnmapViewOfFile(self.pointer)
        kernel.CloseHandle(self.handle)

    def read(self):
        identity = process_identity(self.record['process_id'])
        if not identity or identity['created'] != self.record.get('process_created', identity['created']):
            raise ValueError('运行实例已退出或发生变化。')
        for _ in range(8):
            sequence = self.memory.sequence
            if sequence & 1:
                continue
            copy = Snapshot.from_buffer_copy(self.memory.snapshot)
            if sequence != self.memory.sequence:
                continue
            if self.memory.protocol != 1 or copy.pid != self.record['process_id'] or copy.created != identity['created']:
                raise ValueError('配置通道身份或协议不匹配。')
            return {'status': copy.status, 'revision': copy.revision, 'request_id': copy.request_id,
                    'completed_id': copy.completed_id, 'sampled_at_ms': copy.sampled_at_ms,
                    'fire_output': copy.fire_output.decode(), 'manual_fire_input': copy.manual_fire_input.decode(),
                    'message': copy.message.decode('utf-8', errors='replace'),
                    'regions': [{'effective': r.effective, 'learned': r.learned, 'confidence': r.confidence,
                                 'samples': r.samples} for r in copy.regions]}
        return None

    def reload(self):
        state = self.read()
        if not state:
            raise ValueError('通道正在更新，请稍后再试。')
        if state['status'] == 1:
            raise ValueError('已有重载正在等待视觉帧；这次请求未提交，请完成后再次热重载。')
        request_id = uuid.uuid4().int & ((1 << 63) - 1)
        event = kernel.OpenEventW(2, False, f'Local\\cod_native_reload_{self.record["process_id"]}')
        if not event:
            raise ValueError('无法打开原生热重载事件')
        try:
            self.memory.requested_id = request_id
            if not kernel.SetEvent(event):
                raise OSError('无法发送热重载请求')
        finally:
            kernel.CloseHandle(event)
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            state = self.read()
            if state and state['request_id'] == request_id:
                if state['status'] != 1:
                    return state
                pending = state
            time.sleep(.03)
        if 'pending' in locals():
            return pending  # Accepted, waiting for a fresh frame; not reported as applied.
        raise ValueError('原生程序未确认重载请求，当前参数是否生效尚未确定。')


def learning_state(record):
    channel = ControlChannel(record)
    try:
        return channel.read()
    finally:
        channel.close()


def reload_config(record):
    channel = ControlChannel(record)
    try:
        return channel.reload()
    finally:
        channel.close()
