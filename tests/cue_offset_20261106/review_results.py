"""Summarize controlled replay differences; no ground-truth accuracy verdict."""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[2] / 'runs/cue-offset-20261106')
OUT = parser.parse_args().output


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as source:
        return list(csv.DictReader(source))


review = []
for folder, names in [(OUT, ['E15', 'E17', 'E27', 'E76', 'E41', 'E36', 'E04']),
                      (OUT / 'adjacent', ['v01', 'v06', 'v07', 'q1', 'q2'])]:
    for name in names:
        before = rows(folder / f'{name}-before.csv')
        after = rows(folder / f'{name}-after.csv')
        assert len(before) == len(after)
        gained = [i for i, (b, a) in enumerate(zip(before, after))
                  if b['has_target'] == '0' and a['has_target'] == '1']
        lost = [i for i, (b, a) in enumerate(zip(before, after))
                if b['has_target'] == '1' and a['has_target'] == '0']
        reasons = Counter('/'.join(after[i][k] for k in ['outcome', 'reason', 'gates'])
                          for i in lost)
        review.append(dict(case=name, frames=len(after), gained=gained, lost=lost,
                           lost_reasons=dict(reasons)))
(OUT / 'replay-review.json').write_text(json.dumps(review, indent=2), encoding='utf-8')
for entry in review:
    print(entry['case'], entry['frames'], 'gained', len(entry['gained']), 'lost', len(entry['lost']))

fig, ax = plt.subplots(figsize=(10, 4.5), layout='constrained')
for version, color in [('before', '#bd493e'), ('after', '#207875')]:
    records = rows(OUT / f'material-{version}.csv')
    ax.plot([int(r['frame']) for r in records], [float(r['offset_y']) for r in records],
            label=version, color=color, linewidth=2)
ax.axvspan(30, 44, color='#e8bf54', alpha=.2, label='Cue only (no person detections)')
ax.set(xlabel='Controlled frame index', ylabel='Aim point below cue (capture pixels)',
       title='Real cue pixels + controlled person boxes: contraction, disappearance, recovery')
ax.grid(alpha=.2)
ax.legend(loc='lower right')
fig.savefig(OUT / 'material-offset-comparison.png', dpi=160)
