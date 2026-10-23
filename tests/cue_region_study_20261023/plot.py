"""Export a diagnostic spatial distribution; points are algorithm outputs, not labels."""
from pathlib import Path
import csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import LogNorm

root = Path(__file__).resolve().parents[2]
folder = root / 'runs/cue-region-20261023/videos'
fig, axes = plt.subplots(1, 3, figsize=(13.5, 4.7), constrained_layout=True)
for ax, clip, title in zip(axes, ['downpull', 'dev', 'clean'], ['Oct 22 / two incidents', 'Oct 20 / earlier dev video', 'Oct 20 / later clean video']):
    rows = list(csv.DictReader((folder / (clip + '.csv.positions.csv')).open()))
    all_xy = np.array([[float(r['x']), float(r['y'])] for r in rows if r['kind'] == 'candidate'])
    selected = np.array([[float(r['x']), float(r['y'])] for r in rows if r['kind'] == 'selected_motion'])
    ax.hist2d(all_xy[:, 0], all_xy[:, 1], bins=(64, 52), range=((0, 640), (0, 512)), norm=LogNorm(), cmap='Blues', alpha=.75)
    ax.scatter(selected[:, 0], selected[:, 1], s=3, color='#d76520', alpha=.35, label='Selected motion cue')
    for y, label in [(448, 'Bottom 12.5%'), (384, 'Bottom 25%')]:
        ax.axhline(y, color='#763a83', linewidth=1, linestyle='--')
        ax.text(6, y-6, label, fontsize=8, color='#763a83')
    ax.plot(320, 256, '+', color='black', markersize=10)
    ax.set(xlim=(0, 640), ylim=(512, 0), title=title, xlabel='Crop X (pixels)', ylabel='Crop Y (pixels)')
    ax.set_aspect('equal')
fig.suptitle('Cue locations in the 640 x 512 capture crop\nBlue: detector candidates; orange: selected motion cues (not ground-truth enemies)', fontsize=12)
fig.savefig(root / 'runs/cue-region-20261023/spatial-distribution.png', dpi=170)
