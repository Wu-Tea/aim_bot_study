"""Keep paired per-case evidence, including regressions; no release score."""
import csv
import json
import statistics as st
import sys
from pathlib import Path

root = Path(sys.argv[1])
development = [2026101841, 32452897]
independent = [49979711, 104395367]


def read(folder, prefix, seed):
    with (root / folder / f'{prefix}-{seed}.csv').open() as f:
        return list(csv.DictReader(f))


def compare(before, after, seeds, prefix):
    pairs = []
    for seed in seeds:
        old, new = read(before, prefix, seed), read(after, prefix, seed)
        assert len(old) == len(new) == (80 if prefix == 'seed' else 20)
        for a, b in zip(old, new):
            identity = ['seed', 'case', 'duration_ms'] if prefix == 'seed' else ['seed', 'kind', 'nonlinear', 'duration']
            assert all(a[k] == b[k] for k in identity)
            pairs.append({'before': a, 'after': b})
    metrics = (['mean_error', 'mean_late_error', 'within_4_fraction', 'center_crossings',
                'late_peak', 'late_rms', 'handoffs', 'first_2_ms', 'first_stable_4_ms']
               if prefix == 'seed' else ['late_mean', 'late_rms', 'peak', 'within_half', 'delivered_work', 'handoffs',
                                        'raw_measurement_rms', 'control_measurement_rms'])
    groups = []
    for kind in range(5):
        rows = [p for p in pairs if int(p['after']['kind']) == kind]
        stats = {}
        for key in metrics:
            stats[key] = {}
            for side in ['before', 'after']:
                values = [float(p[side][key]) for p in rows]
                valid = [v for v in values if v >= 0]
                stats[key][side] = st.mean(valid) if valid else None
                stats[key][side + '_missing'] = len(values) - len(valid)
        groups.append({'kind': kind, 'cases': len(rows), 'metrics': stats})
    return {'groups': groups, 'per_case': pairs}


result = {'development_seeds': development, 'independent_seeds': independent,
          'live_acceptance': False, 'recorded_captures': 93, 'solver_cases': 92}
for phase, seeds, before, after in [
    ('development', development, 'before-final', 'development-accepted'),
    ('independent', independent, 'independent-before', 'independent-dev')]:
    result[phase] = {prefix: compare(before, after, seeds, prefix) for prefix in ['seed', 'focused']}
result['ablation'] = {prefix: compare('development-off', 'development-accepted', development, prefix)
                      for prefix in ['seed', 'focused']}
result['cases_per_revision'] = 400
result['ticks_per_revision'] = 4_400_000
(root / 'results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
legacy = []
for seeds, before, after in [(development, 'legacy-before', 'legacy-dev'),
                             (independent, 'legacy-independent-before', 'legacy-independent-dev')]:
    for seed in seeds:
        old, new = read(before, 'seed', seed), read(after, 'seed', seed)
        assert len(old) == len(new) == 64
        for a, b in zip(old, new):
            assert all(a[k] == b[k] for k in ['seed', 'case', 'duration_ms'])
            legacy.append({'before': a, 'after': b})
legacy_groups = []
for cue in ['0', '1']:
    rows = [p for p in legacy if p['after']['kind'] == '1' and p['after']['enemy_evidence'] == cue]
    legacy_groups.append({'enemy_evidence': cue, 'cases': len(rows), 'late_error': {
        side: st.mean(float(p[side]['mean_late_error']) for p in rows) for side in ['before', 'after']}})
(root / 'legacy-results.json').write_text(json.dumps({'moving_groups': legacy_groups, 'per_case': legacy}, indent=2))
for phase in ['development', 'independent', 'ablation']:
    print(phase)
    for prefix in ['seed', 'focused']:
        key = 'mean_late_error' if prefix == 'seed' else 'late_mean'
        print(prefix, [(g['kind'], g['metrics'][key]) for g in result[phase][prefix]['groups']])
