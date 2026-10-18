"""Inspect the existing full-chain trace; no production/controller mutation."""
import csv
import json
import math
import statistics
from pathlib import Path

root = Path(__file__).resolve().parents[2]
trace = root / 'runs/evidence-noise-20261018/legacy-diagnostic-dev/trace-2026101841-8.csv'
with trace.open() as f:
    rows = [r for r in csv.DictReader(f) if 400 <= int(r['tick']) < 1400 and r['mode'] == '2']
ratios = [math.hypot(float(r['request_x']), float(r['request_y'])) / float(r['authority']) for r in rows]
result = {
    'source': str(trace), 'bodylock_ticks_400_1399': len(rows),
    'cap_hit_ticks': sum(v >= .999 for v in ratios),
    'mean_budget_fraction': statistics.mean(ratios), 'max_budget_fraction': max(ratios),
    'max_final_request_difference': max(math.hypot(
        float(r['output_x']) - float(r['request_x']),
        float(r['output_y']) - float(r['request_y'])) for r in rows),
}
out = root / 'runs/bodylock-authority-20261018'
out.mkdir(parents=True, exist_ok=True)
(out / 'trace-analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
