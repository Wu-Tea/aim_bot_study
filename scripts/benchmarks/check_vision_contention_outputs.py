"""Hardware correctness check of all benchmark variants, not timing acceptance."""
import argparse
import os
import time
from pathlib import Path
from run_vision_contention import ROOT, Child, Sampler, digest, rows, save, verify_signatures


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--fixture',type=Path,default=ROOT/'artifacts/vision-gpu-budget-20260907/training-fixture.bgra')
    args=parser.parse_args()
    output=args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
    model=ROOT/'models/best_480x384.engine'
    exe=ROOT/'native/vision_native/build/Release/vision_contention_benchmark.exe'
    count=args.fixture.stat().st_size//(640*512*4)
    save(output/'contract.json',dict(identities={str(p):digest(p) for p in [exe,model,args.fixture]},
        variants=['baseline','no-flush','no-graph'],hz=20,seconds=10,
        gate='All source images covered; every frame signature identical across variants',
        domain='Hardware correctness only; no performance verdict'))
    env={k.upper():v for k,v in os.environ.items()}
    env['PATH']=str(exe.parent)+os.pathsep+env['PATH']
    sampler=Sampler(); oracle=None; results=[]
    try:
        for variant in ['baseline','no-flush','no-graph']:
            child=Child([exe,model,args.fixture,output/variant,variant],output/f'{variant}.log',env)
            try:
                child.receive('READY '); child.send('20 10')
                start=float(child.receive('START ').split()[1]); samples=[]
                while time.perf_counter()<start+10:
                    time.sleep(.5); sample=sampler.sample(child.proc.pid); samples.append(sample)
                    if sample['t']>start+3 and sample['process_peak_engine_pct'] is not None and sample['process_peak_engine_pct']>15:
                        raise RuntimeError('Vision sampled GPU budget exceeded')
                child.receive('DONE ',15)
            finally:
                child.close()
            save(output/f'{variant}-gpu.json',samples)
            timings=rows(output/f'{variant}-0.csv')
            exact,signatures=verify_signatures(rows(output/f'{variant}-0-detections.csv'),len(timings),count,oracle)
            if oracle is None: oracle=signatures
            results.append(dict(variant=variant,frames=len(timings),sources=len(signatures),exact=exact))
        passed=all(r['exact'] for r in results)
        save(output/'result.json',dict(passed=passed,results=results))
        print({'passed':passed,'results':results})
        return 0 if passed else 2
    finally:
        sampler.close()


if __name__=='__main__':
    raise SystemExit(main())
