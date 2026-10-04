"""Game-independent, serial Vision A/A or ABBA with a separate fixed D3D11 load.

No desktop capture, game, controller, ETW, elevated priority, or admin required.
The generated contract is frozen before starting either executable.
"""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import queue
import subprocess
import threading
import time

from vision_gpu_sampler import Sampler, stats

ROOT = Path(__file__).resolve().parents[3]



def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf-8')


class Child:
    def __init__(self, command, log, env):
        self.proc = subprocess.Popen(list(map(str, command)), stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env,
            creationflags=subprocess.CREATE_NO_WINDOW | subprocess.NORMAL_PRIORITY_CLASS)
        self.lines = queue.Queue()
        self.log = Path(log).open('w', encoding='utf-8')
        self.adapter = None
        self.thread = threading.Thread(target=self.read, daemon=True)
        self.thread.start()

    def read(self):
        for line in self.proc.stdout:
            self.log.write(line); self.log.flush()
            self.lines.put(line.strip())

    def send(self, line):
        self.proc.stdin.write(line+'\n'); self.proc.stdin.flush()

    def receive(self, prefix, timeout=90):
        deadline = time.perf_counter()+timeout
        while time.perf_counter()<deadline:
            try:
                line = self.lines.get(timeout=.1)
            except queue.Empty:
                if self.proc.poll() is not None:
                    raise RuntimeError(f'Child exited {self.proc.returncode}; see process log')
                continue
            if line.startswith('ADAPTER '):
                self.adapter=line
            if line.startswith(prefix):
                return line
        raise RuntimeError(f'Timeout waiting for {prefix}')

    def close(self):
        if self.proc.poll() is None:
            self.proc.stdin.close()
            try:
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.proc.terminate(); self.proc.wait(timeout=8)
        self.thread.join(timeout=2)
        self.log.close()


def rows(path):
    with Path(path).open(newline='', encoding='utf-8') as f:
        return list(csv.DictReader(f))


def verify_signatures(records, frame_count, source_count, oracle):
    signatures={}
    exact=len(records)==frame_count
    for index,r in enumerate(records):
        source=int(r['source']); signature=r['signature']
        exact &= int(r['frame'])==index and source==index%source_count
        if source in signatures and signatures[source]!=signature:
            exact=False
        signatures[source]=signature
    exact &= set(signatures)==set(range(source_count))
    if oracle is not None:
        exact &= signatures==oracle
    return exact,signatures



def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, default=ROOT/'artifacts/vision-gpu-budget-20260907/training-fixture.bgra')
    parser.add_argument('--model', type=Path, default=ROOT/'models/best_480x384.engine')
    parser.add_argument('--bin-dir', type=Path, default=ROOT/'native/build/Release')
    parser.add_argument('--candidate', choices=['baseline','no-flush','no-graph'], default='baseline')
    parser.add_argument('--hz', type=float, default=60)
    parser.add_argument('--seconds', type=float, default=24)
    parser.add_argument('--warmup', type=float, default=6)
    parser.add_argument('--load-passes', type=int, default=4)
    parser.add_argument('--load-hz', type=float, default=120)
    args=parser.parse_args()
    if not (0<args.hz<=1000 and 0<args.load_hz<=1000 and 1<=args.load_passes<=128
            and 0<=args.warmup and args.warmup+13<=args.seconds<=170):
        parser.error('Invalid rate, duration, warmup or load passes')
    if args.fixture.stat().st_size%(640*512*4):
        parser.error('Invalid raw fixture size')
    count=args.fixture.stat().st_size//(640*512*4)
    if not 0<count<=4096:
        parser.error('Fixture must contain 1..4096 BGRA frames')
    output=args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    env={k.upper():v for k,v in os.environ.items()}
    env['PATH']=str(args.bin_dir.resolve())+os.pathsep+env['PATH']
    vision_exe=args.bin_dir/'vision_contention_benchmark.exe'
    load_exe=args.bin_dir/'vision_graphics_load.exe'
    schedule=['baseline','baseline'] if args.candidate=='baseline' else ['baseline',args.candidate,args.candidate,'baseline']
    contract=dict(schema=1, domain='synthetic D3D11 handoff + real TensorRT, not DXGI/live acceptance',
         schedule=schedule, parameters=vars(args)|{'fixture':str(args.fixture),
        'model':str(args.model),'output':str(output),'bin_dir':str(args.bin_dir)}, sources=count,
        limitations=['No DXGI acquire/release, compositor, game CPU/DX12 submission, controller or live frame age',
            'Sampled GPU gate is not an instantaneous utilization guarantee',
            'NVML clocks use device 0; adapter CUDA index must be 0; use a single-GPU test host'])
    save(output/'contract.json',contract)
    children=[]; sampler=None; phases=[]; oracle=None
    try:
        sampler=Sampler()
        save(output/'hardware.json',sampler.identity())
        # Reject occupied machines before spending time initializing either workload.
        idle=[]
        for _ in range(4):
            time.sleep(.5); idle.append(sampler.sample(-1))
        save(output/'preflight-gpu.json',idle)
        if any(s['system_peak_engine_pct'] is None or s['system_peak_engine_pct']>3 for s in idle[1:]):
            raise RuntimeError('External GPU busy (>3% busiest engine) or telemetry missing; rerun when idle')
        load=Child([load_exe,output/'load'],output/'load.log',env); children.append(load)
        load.receive('READY ')
        if not load.adapter or not load.adapter.endswith('CUDA 0'):
            raise RuntimeError('NVML/CUDA adapter identity unsupported')
        for phase,variant in enumerate(schedule):
            vision=Child([vision_exe,args.model,args.fixture,output/f'vision-{phase}',variant],output/f'vision-{phase}.log',env)
            children.append(vision); vision.receive('READY ')
            if vision.adapter!=load.adapter:
                raise RuntimeError('Graphics and Vision adapters differ')
            load.send(f'{args.load_hz} {args.seconds+2} {args.load_passes}')
            time.sleep(2)
            vision.send(f'{args.hz} {args.seconds}')
            start=float(vision.receive('START ').split()[1])
            samples=[]
            with (output/f'gpu-{phase}.jsonl').open('x',encoding='utf-8') as log:
                while time.perf_counter()<start+args.seconds:
                    time.sleep(.5)
                    sample=sampler.sample(vision.proc.pid)
                    processes=sample['external_process_engines']
                    load_engines=processes.get(str(load.proc.pid),{})
                    sample['load_gpu_pct']=max(load_engines.values(), default=None)
                    external={}
                    for pid,engines in processes.items():
                        if pid not in (str(load.proc.pid),str(vision.proc.pid)):
                            for engine,value in engines.items():
                                external[engine]=external.get(engine,0)+value
                    sample['unowned_gpu_pct']=max(external.values(),default=0)
                    log.write(json.dumps(sample)+'\n'); log.flush(); samples.append(sample)
                    if sample['t']>=start+args.warmup+1 and sample['t']<start+args.seconds-1:
                        value=sample['process_peak_engine_pct']
                        if value is not None and value>15:
                            raise RuntimeError(f'Phase {phase}: Vision sampled GPU {value:.3f}% exceeds 15%')
                    if vision.proc.poll() is not None or load.proc.poll() is not None:
                        raise RuntimeError('A benchmark child exited unexpectedly')
            vision.receive('DONE ',30); load.receive('DONE ',30)
            vision.close(); children.remove(vision)
            all_rows=rows(output/f'vision-{phase}-0.csv')
            selected=[r for r in all_rows if args.warmup<=float(r['t'])<args.seconds-1]
            selected_load=[r for r in rows(output/f'load-{phase}.csv') if start+args.warmup<=float(r['qpc_s'])<start+args.seconds-1]
            gpu=[s for s in samples if start+args.warmup+1<=s['t']<start+args.seconds-1]
            if not selected or not selected_load or not gpu:
                raise RuntimeError('Missing steady-state rows')
            exact,signatures=verify_signatures(rows(output/f'vision-{phase}-0-detections.csv'),len(all_rows),count,oracle)
            if oracle is None:
                oracle=signatures; save(output/'oracle.json',oracle)
            coverage=len(gpu)>=20 and all(s['process_peak_engine_pct'] is not None and s['load_gpu_pct'] is not None for s in gpu)
            result=dict(phase=phase,variant=variant,frames=len(selected),outputs_exact=exact,
                gpu_coverage=coverage,actual_hz=len(selected)/(args.seconds-1-args.warmup),
                load_hz=len(selected_load)/(args.seconds-1-args.warmup),
                load_valid=all(r['valid']=='1' for r in selected_load),
                load_skipped=sum(int(r['skipped']) for r in selected_load))
            for key in ('wall_ms','copy_submit_ms','map_ms','unmap_ms','preprocess_ms','infer_ms','output_sync_ms','decode_ms'):
                result[key]=stats([float(r[key]) for r in selected])
            result['load_wall_ms']=stats([float(r['wall_ms']) for r in selected_load])
            result['load_gpu_ms']=stats([float(r['gpu_ms']) for r in selected_load])
            ready=[float(r['qpc_s'])+float(r['wall_ms'])/1000 for r in selected]
            result['result_interval_ms']=stats([1000*(b-a) for a,b in zip(ready,ready[1:])])
            for key,name in [('process_peak_engine_pct','vision_gpu'),('load_gpu_pct','load_gpu'),('sm_mhz','sm_mhz'),('unowned_gpu_pct','unowned_gpu')]:
                values=[s[key] for s in gpu if s[key] is not None]
                if not values:
                    raise RuntimeError(f'No valid samples for {name}')
                result[name]=stats(values)
            phases.append(result); save(output/'summary.json',phases)
            print(json.dumps({k:result[k] for k in ['phase','variant','actual_hz','wall_ms','vision_gpu','load_hz','load_wall_ms','sm_mhz','outputs_exact']},ensure_ascii=False),flush=True)
        print(json.dumps(dict(status='MEASURED', completed_phases=len(phases)),ensure_ascii=False),flush=True)
        return 0
    except Exception as e:
        verdict=dict(status='INVALID',reasons=[str(e)],completed_phases=len(phases))
        save(output/'verdict.json',verdict)
        print(json.dumps(verdict,ensure_ascii=False),flush=True)
        return 2
    finally:
        for child in reversed(children):
            child.close()
        if sampler:
            sampler.close()


if __name__=='__main__':
    raise SystemExit(main())
