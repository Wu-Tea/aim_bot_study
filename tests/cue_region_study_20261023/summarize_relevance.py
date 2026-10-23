from pathlib import Path
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
    rows = np.genfromtxt(folder / (clip + '.csv'), delimiter=',', names=True)
    assert len(rows) == n and np.array_equal(rows['frame'], np.arange(n))
    timed = rows[rows['frame'] >= 30]
    result = {'frames': n, 'selector_differences': int(rows['record_diff'].sum()),
              'full_cues': int(rows['full_cues'].sum()), 'greedy_cues': int(rows['greedy_cues'].sum())}
    for field in ['full_cue_ms', 'greedy_cue_ms', 'full_selector_ms', 'greedy_selector_ms']:
        result[field] = dict(zip(['mean', 'p50', 'p95'], map(float, [np.mean(timed[field]), *np.quantile(timed[field], [.5, .95])])))
    if 'native_selector_ms' in rows.dtype.names:
        result['native_selector_ms'] = dict(zip(['mean', 'p50', 'p95'], map(float, [np.mean(timed['native_selector_ms']), *np.quantile(timed['native_selector_ms'], [.5, .95])])))
    summary[clip] = result
print(json.dumps(summary, indent=2))
if len(clips) == 3:
    (folder / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
