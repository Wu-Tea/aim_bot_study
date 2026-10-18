"""Paired per-case statistics; simulation is evidence, not live acceptance."""
import csv
import json
import statistics as st
import sys
from pathlib import Path

root = Path(sys.argv[1])
development = [2026101829, 49979693]
independent = [86028157, 104395337]
metrics = ['mean_error', 'mean_late_error', 'within_4_fraction', 'center_crossings',
           'first_20_ms', 'first_2_ms', 'first_stable_4_ms', 'late_peak', 'late_rms',
           'handoffs', 'post_handoff_peak']


def read(folder, seed):
    with (root / folder / f'seed-{seed}.csv').open() as f:
        return list(csv.DictReader(f))


def summarize(pairs):
    groups = []
    for kind, name in enumerate(['static', 'moving', 'firing_occlusion', 'manual']):
        selected = [(b, a) for b, a in pairs if int(a['kind']) == kind]
        group = dict(name=name, cases=len(selected),
                     improved_mean_error=sum(float(a['mean_error']) < float(b['mean_error']) for b, a in selected), metrics={})
        for key in metrics:
            values = [[float(row[side][key]) for row in selected] for side in (0, 1)]
            group['metrics'][key] = {
                label: (st.mean(v for v in vals if v >= 0) if any(v >= 0 for v in vals) else None)
                for label, vals in zip(['before', 'after'], values)}
            group['metrics'][key].update({label: sum(v < 0 for v in vals)
                                         for label, vals in zip(['before_missing', 'after_missing'], values)})
        groups.append(group)
    return groups


result = dict(development_seeds=development, independent_seeds=independent,
              recorded_captures=93, recorded_solver_cases=92, live_game_acceptance=False)
all_pairs = []
for phase, seeds, before, after in [('development', development, 'development-before', 'development-final'),
                                   ('independent', independent, 'independent-before', 'independent-dev')]:
    pairs = []
    for seed in seeds:
        b, a = read(before, seed), read(after, seed)
        assert len(b) == len(a) == 64
        for old, new in zip(b, a):
            assert (old['seed'], old['case'], old['duration_ms']) == (new['seed'], new['case'], new['duration_ms'])
        pairs.extend(zip(b, a))
    result[phase] = summarize(pairs)
    all_pairs.extend(pairs)
result['combined'] = summarize(all_pairs)
result['cases_per_revision'] = len(all_pairs)
result['ticks_per_revision'] = sum(int(a['duration_ms']) for b, a in all_pairs)
result['per_case'] = [dict(before=b, after=a) for b, a in all_pairs]
(root / 'results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
for phase in ['development', 'independent', 'combined']:
    print(phase)
    for g in result[phase]:
        print(g['name'], 'improved', g['improved_mean_error'], '/', g['cases'],
              {k: {s: round(v, 3) if v is not None else None for s, v in g['metrics'][k].items()}
               for k in metrics})
