"""Behavior checks on the final exports, not an accuracy or release score."""
import csv,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]/'runs/cue-motion-20261025'
summary={};compared=0
for q in ['q1','q2']:
    before=[json.loads(s) for s in (root/'before-hdr'/q/'vision.jsonl').read_text().splitlines()]
    after=[json.loads(s) for s in (root/'release-hdr'/q/'vision.jsonl').read_text().splitlines()]
    assert len(before)==len(after)
    assert all(a['t']==b['t'] and [(d['box'],d['conf'],d['class']) for d in a['detections']]==
        [(d['box'],d['conf'],d['class']) for d in b['detections']] for a,b in zip(before,after))
    lo,hi=(796,857) if q=='q1' else (1596,1639)
    assert all(r['has_target'] and r['source']=='cue_hold' and not r['fire_authority'] for r in after[lo:hi+1])
    summary[q]=dict(frames=len(after),lost=sum(a['has_target'] and not b['has_target'] for a,b in zip(before,after)),
        gained=sum(not a['has_target'] and b['has_target'] for a,b in zip(before,after)),
        window=dict(first=lo,last=hi,time_start=after[lo]['t'],time_end=after[hi]['t'],
            before=sum(r['has_target'] for r in before[lo:hi+1]),after=hi-lo+1))
    for iteration in range(3):
        cpu=list(csv.DictReader((root/'validation-release'/f'{q}-{iteration}-after.csv').open()))
        assert len(cpu)==len(after)
        for a,b in zip(after,cpu):
            assert a['has_target']==bool(int(b['target'])) and a['source']==b['source'] and a['generation']==int(b['generation'])
            assert a['selected']==int(b['selected']) and abs(a['target'][0]-float(b['x']))<1e-4 and abs(a['target'][1]-float(b['y']))<1e-4
            compared+=1
old=list(csv.DictReader((root/'validation-release/v07-0-after.csv').open()))
assert old[21803]['source']=='observed','Previously fixed full-body recovery must remain immediate'
summary['cpu_gpu_compared']=compared
(root/'behavior-release.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
