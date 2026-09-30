"""Windows PDH process GPU engines + NVML device clock; missing samples remain missing."""
import ctypes as C
from ctypes import wintypes as W
import time
import numpy as np

class Value(C.Structure):
    _fields_=[('status',W.DWORD),('value',C.c_double)]
class Item(C.Structure):
    _fields_=[('name',W.LPWSTR),('value',Value)]
class Util(C.Structure):
    _fields_=[('gpu',C.c_uint),('memory',C.c_uint)]
def checked(code):
    if code != 0:
        raise RuntimeError(f"GPU telemetry API failed: {code & 0xffffffff:#x}")


class Sampler:
    def __init__(self):
        self.p=C.WinDLL('pdh.dll'); self.query=W.HANDLE(); self.counter=W.HANDLE()
        self.p.PdhOpenQueryW.argtypes=[W.LPCWSTR,C.c_size_t,C.POINTER(W.HANDLE)]
        self.p.PdhAddEnglishCounterW.argtypes=[W.HANDLE,W.LPCWSTR,C.c_size_t,C.POINTER(W.HANDLE)]
        self.p.PdhCollectQueryData.argtypes=[W.HANDLE]
        self.p.PdhGetFormattedCounterArrayW.argtypes=[W.HANDLE,W.DWORD,C.POINTER(W.DWORD),C.POINTER(W.DWORD),C.c_void_p]
        self.p.PdhCloseQuery.argtypes=[W.HANDLE]
        checked(self.p.PdhOpenQueryW(None,0,C.byref(self.query)))
        checked(self.p.PdhAddEnglishCounterW(self.query,r'\GPU Engine(*)\Utilization Percentage',0,C.byref(self.counter)))
        checked(self.p.PdhCollectQueryData(self.query))
        self.n=C.WinDLL('nvml.dll'); checked(self.n.nvmlInit_v2())
        self.device=C.c_void_p(); checked(self.n.nvmlDeviceGetHandleByIndex_v2(0,C.byref(self.device)))
    def identity(self):
        count=C.c_uint(); checked(self.n.nvmlDeviceGetCount_v2(C.byref(count)))
        if count.value != 1:
            raise RuntimeError('This benchmark currently requires a single-GPU host')
        result={}
        for label,func,args in [
            ('gpu', self.n.nvmlDeviceGetName, [self.device]),
            ('uuid', self.n.nvmlDeviceGetUUID, [self.device]),
            ('driver', self.n.nvmlSystemGetDriverVersion, [])]:
            buf=C.create_string_buffer(256)
            checked(func(*args,buf,len(buf)))
            result[label]=buf.value.decode('utf-8')
        return result
    def sample(self,pid):
        checked(self.p.PdhCollectQueryData(self.query))
        size=W.DWORD(); count=W.DWORD()
        code=self.p.PdhGetFormattedCounterArrayW(self.counter,0x200,C.byref(size),C.byref(count),None)
        if code & 0xffffffff != 0x800007d2: raise RuntimeError(('PDH size',hex(code & 0xffffffff)))
        buf=C.create_string_buffer(size.value)
        result=self.p.PdhGetFormattedCounterArrayW(self.counter,0x200,C.byref(size),C.byref(count),buf)
        if result & 0xffffffff == 0xc0000bba:
            count.value=0  # Newly appearing instances lack the preceding sample; preserve missing coverage.
        elif result: raise RuntimeError(('PDH array',hex(result & 0xffffffff),size.value,count.value))
        items=C.cast(buf,C.POINTER(Item)); own={}; total={}; external={}; processes={}; valid=0
        for i in range(count.value):
            item=items[i]
            if item.value.status not in (0,1): continue
            name=item.name; val=max(0,item.value.value); valid+=1
            engine=name.split('_luid_',1)[-1]
            total[engine]=total.get(engine,0)+val
            process_id=name.split('_')[1]
            if val>0: processes.setdefault(process_id,{})[engine]=val
            if not name.startswith(f'pid_{pid}_'): external[engine]=external.get(engine,0)+val
            if name.startswith(f'pid_{pid}_'): own[engine]=own.get(engine,0)+val
        util=Util(); clock=C.c_uint(); power=C.c_uint()
        checked(self.n.nvmlDeviceGetUtilizationRates(self.device,C.byref(util)))
        checked(self.n.nvmlDeviceGetClockInfo(self.device,1,C.byref(clock)))
        checked(self.n.nvmlDeviceGetPowerUsage(self.device,C.byref(power)))
        return dict(t=time.perf_counter(),pdh_status=hex(result & 0xffffffff),process_peak_engine_pct=max(own.values(),default=None),process_engines=own,external_process_engines=processes,external_peak_engine_pct=max(external.values(),default=None),system_peak_engine_pct=max(total.values(),default=None),nvml_gpu_pct=util.gpu,sm_mhz=clock.value,power_w=power.value/1000,valid_counters=valid)
    def close(self):
        self.p.PdhCloseQuery(self.query); self.n.nvmlShutdown()

def stats(values):
    a=np.asarray(values,dtype=float)
    return dict(n=len(a),mean=float(a.mean()),p50=float(np.percentile(a,50)),p95=float(np.percentile(a,95)),p99=float(np.percentile(a,99)),max=float(a.max()))
