"""Summarize paired scenarios without turning aggregate scores into acceptance."""
import csv
import json
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
seeds = [49980167, 104396113]
versions = ['pre', 'before', 'after']
data = {v: sum([list(csv.DictReader((root / v / str(s) / 'cases.csv').open()))
                for s in seeds], []) for v in versions}
for v in versions:
    assert [(r['seed'], r['case'], r['duration']) for r in data[v]] == [
        (r['seed'], r['case'], r['duration']) for r in data['after']]
groups = {
    'all': lambda r: True,
    'tracking': lambda r: int(r['manual']) in [0, 1, 3],
    'fast_tracking': lambda r: int(r['manual']) in [0, 1, 3] and int(r['motion']) in [2, 3],
    'stationary': lambda r: int(r['manual']) in [0, 1, 3] and int(r['motion']) == 0,
    'player_strafe': lambda r: int(r['manual']) in [0, 1, 3] and int(r['motion']) == 4,
    'firing': lambda r: int(r['manual']) == 5,
    'manual_stress': lambda r: int(r['manual']) in [2, 4, 6, 7],
}
result = {'seeds': seeds, 'versions': {'pre': '6496f84', 'before': '998275a', 'after': 'dev working tree'},
          'cases_per_version': 192, 'ticks_per_version': 2112000, 'groups': {}}
for group, include in groups.items():
    indices = [i for i, r in enumerate(data['after']) if include(r)]
    delta = [float(data['after'][i]['late_error']) - float(data['before'][i]['late_error']) for i in indices]
    worst = sorted(zip(delta, indices), reverse=True)[:5]
    result['groups'][group] = {
        'count': len(indices),
        'means': {v: {m: statistics.mean(float(data[v][i][m]) for i in indices)
                      for m in ['mean_error', 'late_error', 'mean_delta', 'peak_delta']} for v in versions},
        'better_worse_flat_at_0_01px': [sum(d < -.01 for d in delta), sum(d > .01 for d in delta), sum(abs(d) <= .01 for d in delta)],
        'worst': [{'delta': d, 'after': data['after'][i], 'before': data['before'][i]} for d, i in worst],
    }
(root / 'summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({g: {v: round(x['means'][v]['late_error'], 4) for v in versions}
                  for g, x in result['groups'].items()}, indent=2))
