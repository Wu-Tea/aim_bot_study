"""Summarize completed region experiments; never accept partial video output."""
from pathlib import Path
import csv
import json
import re
import sys
import numpy as np

folder = Path(sys.argv[1])
clips = sys.argv[2:] or ['downpull', 'dev', 'clean']
summary = {}
for clip in clips:
    log = (folder / (clip + '.txt')).read_text()
    n = int(re.search(r'frames=(\d+)', log)[1])
    data = np.genfromtxt(folder / (clip + '.csv'), delimiter=',', names=True)
    result = {}
    for limit in [512, 448, 384, 320, 288]:
        rows = data[data['bottom'] == limit]
        assert len(rows) == n and np.array_equal(rows['frame'], np.arange(n))
        timed = rows[rows['frame'] >= 30]
        metrics = {'frames': n}
        for field in ['removed', 'added', 'record_diff', 'selected_diff']:
            metrics[field] = int(rows[field].sum())
        metrics['lost_target_frames'] = int(((rows['full_target'] == 1) & (rows['region_target'] == 0)).sum())
        metrics['extra_target_frames'] = int(((rows['full_target'] == 0) & (rows['region_target'] == 1)).sum())
        metrics['generation_diff_frames'] = int((rows['full_generation'] != rows['region_generation']).sum())
        metrics['first_selected_diff_frame'] = int(rows[rows['selected_diff'] > 0]['frame'][0]) if metrics['selected_diff'] else None
        for field in ['full_cue_ms', 'region_cue_ms', 'full_selector_ms', 'region_selector_ms']:
            metrics[field] = {name: float(value) for name, value in zip(['mean', 'p50', 'p95'], [np.mean(timed[field]), *np.quantile(timed[field], [.5, .95])])}
        result[str(limit)] = metrics
    positions = list(csv.DictReader((folder / (clip + '.csv.positions.csv')).open()))
    result['positions'] = {}
    for kind in ['candidate', 'selected_motion']:
        ys = np.array([float(p['y']) for p in positions if p['kind'] == kind])
        result['positions'][kind] = {'count': len(ys), 'max_y': float(max(ys)) if len(ys) else None,
                                     'at_or_below': {str(y): int((ys >= y).sum()) for y in [288, 320, 384, 448]}}
    summary[clip] = result
print(json.dumps(summary, indent=2))
if len(clips) == 3:
    (folder / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
