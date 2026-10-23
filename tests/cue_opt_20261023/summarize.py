import csv
import json
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'runs/cue-opt-20261023/videos')
summary = {}
for name in ('downpull', 'dev', 'clean'):
    if not (root / (name + '.txt')).exists():
        raise SystemExit('Replay not completed: ' + name)
    with (root / (name + '.csv')).open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if any(int(row['frame']) != i for i, row in enumerate(rows)):
        raise SystemExit('Non-contiguous video frames: ' + name)
    result = dict(frames=len(rows), target_frames=sum(int(row['target']) for row in rows), cue_candidates=sum(int(row['cues']) for row in rows))
    measured = rows[30:]
    for key in ('before_cue_ms', 'after_cue_ms', 'before_selector_ms', 'after_selector_ms'):
        values = sorted(float(row[key]) for row in measured)
        result[key] = dict(mean=statistics.mean(values), p50=statistics.median(values), p95=values[int(.95 * len(values))])
    result['cue_reduction_percent'] = 100 * (1 - result['after_cue_ms']['mean'] / result['before_cue_ms']['mean'])
    summary[name] = result
(root / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary, indent=2))
