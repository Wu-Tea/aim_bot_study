"""Read-only, memory-only native frame counters; no log or file parsing."""
import ctypes
from ctypes import wintypes as w

from .runtime import kernel, process_identity


class Counts(ctypes.Structure):
    _fields_=[(name,ctypes.c_uint64) for name in ('elapsed_ns','aim_ns','vision_frames','aim_frames','controller_ticks',
        'recent_elapsed_ns','recent_aim_ns','recent_vision_frames','recent_aim_frames')]+[
        ('aiming',w.DWORD),('reserved',w.DWORD)]


class FrameSnapshot(ctypes.Structure):
    _fields_=[('pid',w.DWORD),('state',w.DWORD),('created',ctypes.c_uint64),
              ('sampled_at_ms',ctypes.c_uint64),('counts',Counts)]


class FrameMemory(ctypes.Structure):
    _fields_=[('sequence',w.LONG),('protocol',w.DWORD),('snapshot',FrameSnapshot)]


assert ctypes.sizeof(Counts)==80 and ctypes.sizeof(FrameSnapshot)==104
assert ctypes.sizeof(FrameMemory)==112 and FrameMemory.snapshot.offset==8 and FrameSnapshot.counts.offset==24
kernel.OpenFileMappingW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR]
kernel.OpenFileMappingW.restype=w.HANDLE
kernel.MapViewOfFile.argtypes=[w.HANDLE,w.DWORD,w.DWORD,w.DWORD,ctypes.c_size_t]
kernel.MapViewOfFile.restype=ctypes.c_void_p
kernel.UnmapViewOfFile.argtypes=[ctypes.c_void_p]


def read_frame_rates(record):
    # PID + creation time bind counters to the actual running session. Passing
    # the observer's record avoids another filesystem-based active() lookup.
    identity=process_identity(record['process_id'])
    if not identity or identity['created']!=record.get('process_created',identity['created']):return None
    handle=kernel.OpenFileMappingW(4,False,f'Local\\cod_native_fps_{record["process_id"]}')
    if not handle:return None  # Older runtimes do not expose this channel.
    pointer=None
    try:
        pointer=kernel.MapViewOfFile(handle,4,0,0,ctypes.sizeof(FrameMemory))
        if not pointer:raise OSError('无法读取内存帧率通道')
        memory=FrameMemory.from_address(pointer)
        # Bounded seqlock read, matching the native single-writer publication.
        for _ in range(8):
            sequence=memory.sequence
            if sequence&1:continue
            copy=FrameSnapshot.from_buffer_copy(memory.snapshot)
            protocol=memory.protocol
            if sequence!=memory.sequence:continue
            if protocol!=1 or copy.pid!=record['process_id'] or copy.created!=identity['created']:
                raise ValueError('帧率通道身份或协议不匹配')
            if copy.state not in (0,1,2):raise ValueError('帧率通道状态无效')
            counts=copy.counts
            if counts.aim_ns>counts.elapsed_ns or counts.aim_frames>counts.vision_frames or \
               counts.recent_aim_ns>counts.recent_elapsed_ns or counts.recent_aim_frames>counts.recent_vision_frames or \
               counts.recent_elapsed_ns>5_000_000_000 or counts.aiming not in (0,1):
                raise ValueError('帧率通道计数无效')
            result={name:getattr(counts,name) for name,_ in Counts._fields_ if name!='reserved'}
            result.update(pid=copy.pid,created=copy.created,state=copy.state,sampled_at_ms=copy.sampled_at_ms)
            return result
        return None  # A concurrent publication must not produce a torn sample.
    finally:
        if pointer:kernel.UnmapViewOfFile(pointer)
        kernel.CloseHandle(handle)


def rate(frames,duration_ns):
    return frames*1_000_000_000/duration_ns if duration_ns else None
