"""Summarize stage durations; repeats do not count as independent video frames."""
import csv
import json
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'runs/vision-perf-20261023')
result = {}
for path in sorted(root.glob('*-ecores.csv')):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    result[path.stem] = {}
    for mode in ('cue_on', 'legacy'):
        subset = [r for r in rows if r['mode'] == mode and int(r['frame']) >= 30]
        values = {}
        for key in ('inference_ms', 'gpu_infer_ms', 'readback_ms', 'selector_ms', 'cue_ms', 'roi_ratio'):
            ordered = sorted(float(r[key]) for r in subset)
            values[key] = dict(mean=statistics.mean(ordered), p50=statistics.median(ordered), p95=ordered[int(len(ordered) * .95)])
        values['samples'] = len(subset)
        result[path.stem][mode] = values
    # Inference/readback timings are measured once per video frame and reused
    # across CPU rounds and modes; never treat these rows as repeated GPU runs.
(root / 'summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
