"""Functional positive control for the load generator; not a Vision A/B result."""
import argparse
import os
import time
from run_vision_contention import ROOT, Child, Sampler, digest, rows, save, stats


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    from pathlib import Path
    output=Path(args.output).resolve(); output.mkdir(parents=True,exist_ok=False)
    exe=ROOT/'native/build/Release/vision_graphics_load.exe'
    save(output/'contract.json',dict(binary_sha256=digest(exe),schedule=[1,4,1],hz=120,seconds=6,
        gate='All queries valid and shader image nonconstant; 4-pass GPU mean >2x both 1-pass means',
        domain='Functional load-sensitivity check; external load not excluded; not performance acceptance'))
    env={k.upper():v for k,v in os.environ.items()}
    env['PATH']=str(exe.parent)+os.pathsep+env['PATH']
    sampler=Sampler(); child=None; results=[]
    try:
        save(output/'hardware.json',sampler.identity())
        child=Child([exe,output/'load'],output/'load.log',env); child.receive('READY ')
        for phase,passes in enumerate([1,4,1]):
            child.send(f'120 6 {passes}'); start=time.perf_counter(); samples=[]
            while time.perf_counter()<start+6:
                time.sleep(.5); samples.append(sampler.sample(child.proc.pid))
            child.receive('DONE ',15)
            save(output/f'gpu-{phase}.json',samples)
            selected=[r for r in rows(output/f'load-{phase}.csv') if start+2<=float(r['qpc_s'])<start+5]
            if not selected: raise RuntimeError('No eligible graphics frames')
            results.append(dict(passes=passes,all_valid=all(r['valid']=='1' for r in selected),
                gpu_ms=stats([float(r['gpu_ms']) for r in selected])))
        passed=all(r['all_valid'] for r in results) and results[1]['gpu_ms']['mean']>2*max(results[i]['gpu_ms']['mean'] for i in [0,2])
        save(output/'result.json',dict(passed=passed,results=results))
        print({'passed':passed,'gpu_mean_ms':[r['gpu_ms']['mean'] for r in results]})
        return 0 if passed else 2
    finally:
        if child: child.close()
        sampler.close()


if __name__=='__main__':
    raise SystemExit(main())
