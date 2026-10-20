"""Freeze logged manual arbitration inputs; this is not a camera-world replay."""
import csv
import json
from pathlib import Path

source = Path('runs/manual-sticky-20261019/live-windows.jsonl')
out = Path('tests/ads_cooperation_20261019/live.csv')
rows = [json.loads(x) for x in source.read_text().splitlines()]
traces = [x for x in rows if x['type'] == 'ads_acquisition_trace']
fields = ['session_s', 'source_line', 'age_ms', 'error_x', 'error_y', 'reserve_x', 'reserve_y',
          'response', 'manual_x', 'manual_y', 'ai_x', 'ai_y', 'old_x', 'old_y', 'firing']
with out.open('w', newline='') as f:
    w = csv.writer(f)
    w.writerow(fields)
    count = 0
    for r in rows:
        if r['type'] != 'controller_sample' or r.get('aim_mode') != 'ads_snap':
            continue
        candidates = [t for t in traces if t['persistent_target_id'] == r['controller_target_track_id']
                      and t['output_sent_ns'] <= r['sample_ns']]
        if not candidates:
            continue
        t = max(candidates, key=lambda x: x['output_sent_ns'])
        if t['acquisition_state'] != 'acquiring_nominal' or r['manual_correction_x'] or r['manual_correction_y']:
            continue
        age = (r['sample_ns'] - t['capture_acquire_begin_ns']) / 1e6
        if age > 32:
            continue
        w.writerow([r['session_s'], r['source_line'], age, r['control_error_x'], r['control_error_y'],
                    r['stopping_reserve_x'], r['stopping_reserve_y'], r['camera_response_scale'],
                    r['manual_x'], r['manual_y'], r['shaped_assist_x'], r['shaped_assist_y'],
                    r['pre_recoil_x'], r['pre_recoil_y'], int(r['final_fire_button'])])
        count += 1
print(f'{count} frozen controller samples -> {out}')
