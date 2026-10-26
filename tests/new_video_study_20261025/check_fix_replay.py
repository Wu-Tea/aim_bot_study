"""Check final GPU scan and CPU timing replay consumed the same selector behavior."""
import csv,json
from pathlib import Path

root=Path(__file__).resolve().parents[2]/'runs/new-video-fix-20261025'
out=root/'performance-final';differences=[];compared=0
for clip in ['v01','v06','v08']:
    expected=[json.loads(s) for s in (root/'final'/clip/'vision.jsonl').read_text().splitlines()]
    for iteration in range(3):
        actual=list(csv.DictReader((out/f'{clip}-{iteration}-after.csv').open()))
        assert len(actual)==len(expected)
        for a,b in zip(expected,actual):
            compared+=1
            same=(a['frame']==int(b['frame']) and a['has_target']==bool(int(b['target']))
                  and a['source']==b['source'] and a['generation']==int(b['generation'])
                  and a['selected']==int(b['selected'])
                  and abs(a['target'][0]-float(b['x']))<1e-4
                  and abs(a['target'][1]-float(b['y']))<1e-4)
            if not same:differences.append([clip,iteration,a['frame']])
result=dict(compared=compared,differences=differences)
(out/'behavior-equivalence.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))
assert not differences
