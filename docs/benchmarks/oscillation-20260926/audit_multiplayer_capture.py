"""Bounded schema-18 diagnostics after evidence preflight; no gameplay acceptance.

Only files in the declared bounded manifest are read. No row repair, cross-clock
join, or zero-filling. Reversal signatures are screening heuristics, not a claim
that target/player motion was stationary. Percentiles use linear interpolation.
"""
import argparse
from collections import Counter, defaultdict, deque
import hashlib
import json
import math
from pathlib import Path


def distribution(values):
    if not values:
        return {"count": 0}
    v = sorted(values)
    def p(q):
        k = (len(v)-1)*q
        i = int(k)
        return v[i] + (v[min(i+1,len(v)-1)]-v[i])*(k-i)
    return dict(count=len(v), p50=p(.5), p95=p(.95), p99=p(.99), max=v[-1])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    intake = json.loads(args.intake.read_text())
    if intake["status"] == "BLOCKED":
        raise SystemExit("blocked source intake; diagnostics refused")
    manifest = json.loads(args.manifest.read_text())
    records, modes, cohorts, safety = Counter(), Counter(), Counter(), Counter()
    errors, intervals, latencies = defaultdict(list), [], []
    rows, delivered, traces = {}, {}, {}
    duplicate = Counter()
    sources = []
    for source in manifest["cohorts"][0]["files"]:
        if source["kind"] != "telemetry_jsonl":
            continue
        path = Path(source["path"])
        digest = hashlib.sha256()
        with path.open("rb") as f:
            for line in f:
                digest.update(line)
                d = json.loads(line)
                assert d["schema_version"] == 18
                t = d["type"]
                records[t] += 1
                table = {"controller_sample": rows, "delivered_control_sample": delivered,
                         "ads_acquisition_trace": traces}.get(t)
                if table is not None:
                    # Controller tick labels are reused by the ring emitter;
                    # they are not unique controller-sample identities. Sample
                    # sequences belong to separate producer domains: do not join.
                    key = d["sample_seq"] if t != "ads_acquisition_trace" else (
                        d["controller_tick_id"], d["source_frame_id"], d["physical_ads_epoch"])
                    if key in table:
                        duplicate[t] += 1
                        # A ring sample can be emitted again under a later
                        # envelope tick. Verify its actual sample payload.
                        before = {k:v for k,v in table[key].items() if k != "tick_id"}
                        after = {k:v for k,v in d.items() if k != "tick_id"}
                        if before != after:
                            raise ValueError(f"conflicting {t} sample payload: {key}")
                        continue
                    table[key] = d
        observed = next(x for x in intake["cohorts"][0]["files"] if x["id"] == source["id"])
        assert digest.hexdigest() == observed["sha256"], "source changed after preflight"
        sources.append(dict(id=source["id"], sha256=digest.hexdigest(), rows=sum(1 for _ in path.open("rb"))))
    emitted = list(rows.values())
    safety["emitter_out_of_order_sample_pairs"] = sum(
        b["sample_ns"] <= a["sample_ns"] for a,b in zip(emitted,emitted[1:]))
    # The event ring flushes older samples after newer telemetry. Reconstruct
    # only this producer's exact sequence; do not join or interpolate gaps.
    rows = dict(sorted(rows.items()))
    previous = None
    segments, segment = [], []
    spikes = []
    for d in rows.values():
        modes[d["aim_mode"]] += 1
        t = d["sample_ns"]
        if previous:
            dt = (t-previous["sample_ns"])/1e6
            intervals.append(dt)
            safety["nonincreasing_sample_time"] += dt <= 0
        for field in ["final_x", "final_y", "physical_x", "physical_y"]:
            safety["nonfinite_"+field] += not math.isfinite(d[field])
        safety["output_outside_unit_axes"] += max(abs(d["final_x"]),abs(d["final_y"])) > 1.00001
        safety["delivery_failed_controller"] += not d["output_delivered"]
        safety["output_error_controller"] += d["output_error_code"] != 0
        active = d["aim_mode"] in ["ads_snap", "body_lock"]
        if active:
            manual = max(abs(d["physical_x"]),abs(d["physical_y"])) > .15
            firing = d["final_fire_button"]
            cohorts[f'{d["aim_mode"]}|manual={manual}|fire={firing}|{d["bodylock_lifecycle"]}'] += 1
            errors[d["aim_mode"]].append(math.hypot(d["control_error_x"],d["control_error_y"]))
        eligible = d["aim_mode"] == "body_lock" and d["bodylock_lifecycle"] == "observed" and d["controller_target_track_id"] != 0
        same = previous and d["controller_target_track_id"] == previous["controller_target_track_id"] and 0 < dt <= 50
        if not eligible or not same:
            if segment: segments.append(segment)
            segment = []
        if eligible: segment.append(d)
        if active and same and dt <= 10 and previous["aim_mode"] == d["aim_mode"]:
            for axis in "xy":
                change = abs(d["pre_recoil_"+axis]-previous["pre_recoil_"+axis])
                if change > .5 and abs(d["physical_"+axis]-previous["physical_"+axis]) < .03 and abs(d["shaped_assist_"+axis]-previous["shaped_assist_"+axis]) < .03:
                    spikes.append(dict(sample_seq=d["sample_seq"], axis=axis, delta=change, mode=d["aim_mode"]))
        previous = d
    if segment: segments.append(segment)
    signatures = []
    for segment in segments:
        for axis in "xy":
            error_flips, output_flips = deque(), deque()
            eprev = uprev = 0
            hits = []
            for d in segment:
                t = d["sample_ns"]
                for field, threshold, queue, old in [("control_error_"+axis,3,error_flips,eprev), ("pre_recoil_"+axis,.03,output_flips,uprev)]:
                    v = d[field]
                    sign = (1 if v>0 else -1) if abs(v)>threshold else 0
                    if sign and old and sign != old: queue.append(t)
                    while queue and t-queue[0] > 1_000_000_000: queue.popleft()
                    if field.startswith("control"): eprev = sign or eprev
                    else: uprev = sign or uprev
                if len(error_flips)>=4 and len(output_flips)>=4: hits.append(d)
            if hits:
                signatures.append(dict(target=segment[0]["controller_target_track_id"], axis=axis,
                    first_sample_seq=hits[0]["sample_seq"], last_sample_seq=hits[-1]["sample_seq"], hit_rows=len(hits),
                    max_manual=max(abs(d["physical_"+axis]) for d in segment),
                    firing_rows=sum(d["final_fire_button"] for d in segment)))
    for d in delivered.values():
        safety["delivery_failed_receipts"] += not d["control"]["output_delivered"]
    for d in traces.values():
        if d["source_present_steady_available"] and d["vigem_submit_complete_ns"] >= d["source_present_steady_ns"]:
            latencies.append((d["vigem_submit_complete_ns"]-d["source_present_steady_ns"])/1e6)
    result = dict(status="INSUFFICIENT_EVIDENCE", scope=manifest["question"], sources=sources,
        records=dict(records), duplicate_sample_payload=dict(duplicate), modes=dict(modes), cohorts=dict(cohorts),
        safety=dict(safety), controller_rows=len(rows), delivered_rows=len(delivered),
        join=dict(performed=False, reason="independent record-local metrics; reused controller tick labels are not a valid cross-stream join"),
        span_seconds=(list(rows.values())[-1]["sample_ns"]-list(rows.values())[0]["sample_ns"])/1e9,
        controller_sample_interval_ms=distribution(intervals), source_present_to_submit_ms=distribution(latencies),
        control_error_px={k:distribution(v) for k,v in errors.items()}, bodylock_segments=len(segments),
        reversal_screening_signatures=signatures, unexplained_large_handoff_screening=spikes,
        limitations=["full final rotation excluded after malformed final row", "no matched live baseline",
        "unknown game refresh and hardware profile", "screening is not a calibrated gameplay oracle",
        "world target motion/manual movement/recoil cannot be uniquely separated from screen error"])
    args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({k:v for k,v in result.items() if k not in ["sources","cohorts"]},indent=2))


if __name__ == "__main__":
    main()
