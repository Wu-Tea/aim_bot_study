"""Read-only streaming inventory and compact controller extraction of clean logs.

No controller replay or live-game verdict: thresholds in follow-up analysis only
identify review windows. Chunk order is numeric; UTC mapping is session-local.
"""
import collections
import csv
import datetime as dt
import json
import pathlib
import time

ROOT = pathlib.Path('D:/work/AI/yolo-study-001-clean/runs/native_perf/sessions')
OUT = pathlib.Path('runs/hipfire-flick-20261023')
FIELDS = '''tick_id frame_id sample_ns vision_capture_ns physical_x physical_y ai_x ai_y final_x final_y left_trigger right_trigger observed_error_x observed_error_y control_error_x control_error_y controller_target_track_id has_target aim_authority enemy_cue_current enemy_identity_confirmed production_target_source production_target_confidence aim_mode manual_authority_mode assist_control_phase operation_class manual_exit_requested handover_requested handover_braking closing_rate_x closing_rate_y stopping_reserve_x stopping_reserve_y camera_response_scale camera_effect_delay_ms camera_alignment_valid bodylock_error_rate_x bodylock_error_rate_y bodylock_position_stick_x bodylock_position_stick_y bodylock_motion_stick_x bodylock_motion_stick_y bodylock_effective_motion_stick_x bodylock_effective_motion_stick_y bodylock_constraint_reason requested_assist_x requested_assist_y shaped_assist_x shaped_assist_y assist_authority assist_authority_reason bodylock_lifecycle output_delivered recoil_y manual_passthrough_x manual_passthrough_y manual_correction_x manual_correction_y visual_authority'''.split()

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    summaries = []
    for folder in sorted(ROOT.glob('20261023*')):
        start = time.perf_counter()
        counts, modes, stats = collections.Counter(), collections.Counter(), collections.Counter()
        examples = {}
        metadata = None
        files = sorted(folder.glob('*.jsonl'), key=lambda p:int(p.stem.rsplit('_',1)[1]))
        first = last = None
        with (OUT / (folder.name + '.csv')).open('w', newline='') as target:
            writer = csv.writer(target)
            writer.writerow(['local_time', 'elapsed_s'] + FIELDS + ['region_x2', 'region_y2'])
            for path in files:
                with path.open('rb') as source:
                    for line in source:
                        try:
                            row = json.loads(line)
                        except ValueError:
                            stats['invalid_json'] += 1
                            continue
                        kind = row.get('type', 'unknown')
                        counts[kind] += 1
                        if kind not in examples:
                            examples[kind] = row
                        if kind == 'session_metadata':
                            metadata = row
                        if kind != 'controller_sample' or not row.get('sample_ns'):
                            continue
                        elapsed = (row['sample_ns'] - metadata['clock_steady_ns']) / 1e9
                        utc = metadata['clock_utc_ns']/1e9 + elapsed
                        local = dt.datetime.fromtimestamp(utc, dt.timezone(dt.timedelta(hours=8))).isoformat(timespec='milliseconds')
                        first = first or local
                        last = local
                        region = row.get('aim_region', [0,0,0,0])
                        writer.writerow([local, round(elapsed,6)] + [row.get(k,'') for k in FIELDS] + region[2:4])
                        modes[row.get('aim_mode')] += 1
                        stats['ai_nonzero'] += abs(row['ai_x']) + abs(row['ai_y']) > .001
                        stats['physical_strong'] += max(abs(row['physical_x']),abs(row['physical_y'])) > .7
                        stats['hip_ai'] += row['left_trigger'] < .1 and abs(row['ai_x']) + abs(row['ai_y']) > .001
                        stats['hip_fire_ai'] += row['left_trigger'] < .1 and row.get('final_fire_button',False) and abs(row['ai_x']) + abs(row['ai_y']) > .001
                print(folder.name, path.name, 'done', flush=True)
        summary = dict(session=folder.name, bytes=sum(p.stat().st_size for p in files), metadata=metadata, first=first, last=last, counts=counts, modes=modes, stats=stats, scan_seconds=time.perf_counter()-start)
        summaries.append(summary)
        (OUT / (folder.name + '-examples.json')).write_text(json.dumps(examples,indent=2))
        (OUT / 'inventory.json').write_text(json.dumps(summaries,indent=2))
        print(json.dumps(summary), flush=True)

if __name__ == '__main__':
    main()
