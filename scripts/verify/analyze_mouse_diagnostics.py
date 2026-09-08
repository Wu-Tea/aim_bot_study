"""Summarize mouse JSONL evidence without inferring unobserved game motion."""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path


def percentile(hist: Counter, fraction: float):
    count = sum(hist.values())
    if not count:
        return None
    seen = 0
    for value, amount in sorted(hist.items()):
        seen += amount
        if seen >= count * fraction:
            return value


def analyze(directory: Path) -> dict:
    files = sorted(directory.glob('ticks-*.jsonl'))
    if not files:
        raise ValueError(f'No tick segments in {directory}')
    histogram = {name: Counter() for name in ('tick_ms', 'work_ms', 'capture_interval_ms', 'source_age_ms')}
    counts = Counter()
    previous_ns = previous_capture = previous_frame = previous_tick = 0
    last_window = (0, 0)
    last_sign = [0, 0]
    suppressed_run = [0, 0]
    max_suppressed_run = [0, 0]
    mode_counts = Counter()
    totals = {k: [0, 0] for k in ('source', 'aim', 'final', 'submitted', 'cancelled')}
    first_ns = end_ns = 0
    for path in files:
        with path.open(encoding='utf-8') as stream:
            for line in stream:
                try:
                    r = json.loads(line)
                except json.JSONDecodeError:
                    counts['malformed_lines'] += 1
                    continue
                if r.get('type') != 'tick':
                    continue
                counts['ticks'] += 1
                ns, tick = r['ns'], r['tick']
                first_ns = first_ns or ns
                end_ns = ns
                if previous_tick and tick != previous_tick + 1:
                    counts['tick_sequence_gaps'] += 1
                previous_tick = tick
                if previous_ns and ns > previous_ns:
                    histogram['tick_ms'][round((ns - previous_ns) / 1e6, 3)] += 1
                previous_ns = ns
                histogram['work_ms'][round(max(0, r['end_ns'] - ns) / 1e6, 3)] += 1
                if r.get('target'):
                    histogram['source_age_ms'][round(r['source_age_ms'], 2)] += 1
                frame, capture = r.get('vision_frame', 0), r.get('capture_ns', 0)
                if capture and frame != previous_frame:
                    if previous_capture and capture > previous_capture:
                        histogram['capture_interval_ms'][round((capture - previous_capture) / 1e6, 3)] += 1
                    previous_frame, previous_capture = frame, capture
                    counts['distinct_capture_frames'] += 1
                mode_counts[r.get('mode', 'unknown')] += 1
                for key in totals:
                    for axis in range(2):
                        totals[key][axis] += r[key][axis]
                for key in ('failed', 'recoil_clock_gap', 'vision_accepted'):
                    counts[key] += bool(r.get(key))
                counts['recoil_counts_down'] += r.get('recoil_dy', 0)
                window = tuple(r.get('window', [0, 0])[:2])
                if window[1]:
                    if window <= last_window:
                        counts['window_sequence_violations'] += 1
                    last_window = window
                    if r.get('committed') and r['final'] != r['submitted']:
                        counts['committed_count_mismatches'] += 1
                for axis in range(2):
                    # A diagnostic signature, not proof that suppression caused
                    # a live aiming defect. Alternating tremor belongs here too.
                    blocked = (r.get('mode') == 'body_lock' and r['source'][axis] != 0
                               and r['aim'][axis] == 0 and r['retention'][axis] == 0)
                    if blocked:
                        suppressed_run[axis] += abs(r['source'][axis])
                        counts['bodylock_manual_suppressed_ai_idle_ticks'] += 1
                        max_suppressed_run[axis] = max(max_suppressed_run[axis], suppressed_run[axis])
                    else:
                        suppressed_run[axis] = 0
                    sign = (r['aim'][axis] > 0) - (r['aim'][axis] < 0)
                    if sign and last_sign[axis] and sign != last_sign[axis]:
                        counts[f'aim_axis_{axis}_reversals'] += 1
                    if sign:
                        last_sign[axis] = sign
    summary_path = directory / 'summary.json'
    summary = json.loads(summary_path.read_text(encoding='utf-8')) if summary_path.exists() else None
    complete = bool(summary and summary.get('clean_shutdown') and not any(
        summary.get(k) for k in ('dropped', 'discarded', 'writer_failed', 'budget_exhausted'))
        and not counts['malformed_lines'] and not counts['tick_sequence_gaps']
        and summary.get('written') == counts['ticks'])
    timing = {name: {'p50': percentile(hist, .5), 'p95': percentile(hist, .95),
                     'p99': percentile(hist, .99), 'max': max(hist) if hist else None}
              for name, hist in histogram.items()}
    return {'schema': 1, 'session': str(directory.resolve()), 'duration_seconds': (end_ns - first_ns) / 1e9,
            'complete_log': complete, 'counts': dict(counts), 'modes': dict(mode_counts),
            'timing_ms': timing, 'count_totals': totals, 'max_suppressed_run_abs_counts': max_suppressed_run,
            'writer_summary': summary,
            'interpretation': 'Timing and output signatures only. Transport acceptance is not independent game receipt; '
                              'reversals alone do not establish unwanted jitter or its cause.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', nargs='?', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    directory = args.session
    if directory is None:
        candidates = [p.parent for p in Path('runs/mouse').glob('*/session.json')]
        if not candidates:
            parser.error('No mouse sessions yet; run mouse_start.bat first or give a session directory.')
        directory = max(candidates, key=lambda p: p.stat().st_mtime_ns)
    text = json.dumps(analyze(directory), ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(text + '\n', encoding='utf-8')
    print(text)


if __name__ == '__main__':
    main()
