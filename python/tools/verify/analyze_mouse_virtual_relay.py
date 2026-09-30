"""Independent receiver oracle for the standalone virtual mouse relay.

No-source, truncated, simulated, failed, or mismatched sessions never pass.
This proves only the recorded desktop interval, not a game or untested input.
"""
import argparse
import csv
import json
from pathlib import Path


def analyze(directory: Path) -> dict:
    session = json.loads((directory / 'session.json').read_text(encoding='utf-8'))
    with (directory / 'events.csv').open(encoding='utf-8', newline='') as handle:
        records = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(handle)]
    source = [r for r in records if r['kind'] == 1]
    written = [r for r in records if r['kind'] in (2, 5)]
    received = [r for r in records if r['kind'] == 4 and r['qpc'] >= session['armed_at']]
    leaked = [r for r in records if r['kind'] == 3
              and session['armed_at'] <= r['qpc'] <= session['released_at']
              and any(r[key] for key in ('x', 'y', 'buttons', 'wheel', 'hwheel'))]
    problems = []
    if session['simulation']:
        problems.append('simulated_session')
    if not session['ready_observed'] or not session['armed_at'] or not session['released_at']:
        problems.append('capture_interval_not_established')
    if session['worker_exit'] or session['error'] or session['receiver_error'] or session['forced_worker_exit']:
        problems.append('session_failure')
    if session.get('diagnostics_truncated', False):
        problems.append('truncated_evidence')
    if session.get('kernel_exit_pending', False):
        problems.append('kernel_exit_pending')
    if len(source) < 8 or sum(abs(r['x']) + abs(r['y']) for r in source) < 32:
        problems.append('insufficient_physical_source_activity')
    if leaked:
        problems.append('physical_input_leaked_to_receiver')

    # Independently total each captured packet's demand before the next source
    # packet. HID report splitting must not change its displacement or wheels.
    current = None
    for r in [r for r in records if r['kind'] in (1, 2)]:
        if r['kind'] == 1:
            if current and current != [0, 0, 0, 0]:
                problems.append('source_to_output_mismatch')
            sign = {0: 1, 1: 0, 2: -1}.get(r['mode'])
            if sign is None:
                problems.append('invalid_mode')
                sign = 0
            current = [r['x'] * sign, r['y'] * sign,
                       r['wheel'] if r['buttons'] & 0x400 else 0,
                       r['wheel'] if r['buttons'] & 0x800 else 0]
        elif current is None:
            problems.append('output_without_source')
        else:
            for i, key in enumerate(('x', 'y', 'wheel', 'hwheel')):
                current[i] -= r[key] * (120 if i >= 2 else 1)
    if current and current != [0, 0, 0, 0]:
        problems.append('source_to_output_mismatch')

    def motion(rows):
        return [(r['x'], r['y']) for r in rows if r['x'] or r['y']]
    if motion(written) != motion(received):
        problems.append('virtual_motion_sequence_mismatch')
    expected_buttons = []
    held = 0
    for r in written:
        next_held = r['buttons']
        flags = 0
        for i in range(5):
            bit = 1 << i
            if (held & bit) != (next_held & bit):
                flags |= (1 if next_held & bit else 2) << (i * 2)
        if flags:
            expected_buttons.append(flags)
        held = next_held
    observed_buttons = [r['buttons'] & 0x3ff for r in received if r['buttons'] & 0x3ff]
    if expected_buttons != observed_buttons or held:
        problems.append('virtual_button_sequence_or_release_mismatch')
    # Verify button transformation separately; a bad mapper and receiver can
    # otherwise agree on the same wrong virtual button report.
    desired_held = 0
    for r in [r for r in records if r['kind'] in (1, 2, 5)]:
        if r['kind'] == 1:
            for i in range(5):
                edge = (r['buttons'] >> (i * 2)) & 3
                if edge == 1:
                    desired_held |= 1 << i
                elif edge == 2:
                    desired_held &= ~(1 << i)
                elif edge == 3:
                    problems.append('ambiguous_source_button_order')
        elif r['kind'] == 5:
            desired_held = 0
        elif desired_held != r['buttons']:
            problems.append('source_button_to_output_mismatch')
    def wheels(rows, scale):
        return [(r['wheel'] * scale, r['hwheel'] * scale) for r in rows if r['wheel'] or r['hwheel']]
    if wheels(written, 120) != wheels(received, 1):
        problems.append('virtual_wheel_sequence_mismatch')
    mode_activity = []
    for mode, name in ((0, 'pass'), (1, 'block'), (2, 'invert')):
        inputs = [r for r in source if r['mode'] == mode]
        outputs = [r for r in written if r['kind'] == 2 and r['mode'] == mode]
        mode_activity.append({
            'mode': name, 'source_packets': len(inputs),
            'source_absolute_counts': sum(abs(r['x']) + abs(r['y']) for r in inputs),
            'output_absolute_counts': sum(abs(r['x']) + abs(r['y']) for r in outputs),
        })
    return {
        'desktop_interval_passed': not problems, 'problems': sorted(set(problems)),
        'source_packets': len(source), 'virtual_reports_submitted': len(written),
        'virtual_motion_packets_received': len(motion(received)), 'physical_leaked_packets': len(leaked),
        'modes_with_source': sorted({r['mode'] for r in source}),
        'physical_button_flags_exercised': sorted({r['buttons'] & 0x3ff for r in source if r['buttons'] & 0x3ff}),
        'vertical_wheel_exercised': any(r['buttons'] & 0x400 for r in source),
        'horizontal_wheel_exercised': any(r['buttons'] & 0x800 for r in source),
        'mode_activity': mode_activity,
        'three_mode_motion_verified': not problems and all(
            m['source_packets'] >= 8 and m['source_absolute_counts'] >= 32 for m in mode_activity),
        'game_receipt_verified': False, 'rate_1000hz_verified': False,
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = analyze(args.directory)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['desktop_interval_passed'] else 2)
