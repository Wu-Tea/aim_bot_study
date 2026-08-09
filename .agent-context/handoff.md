# Agent Handoff

Last updated: 2026-08-07
Active scope: native C++ FPS gamepad runtime; protected control/Vision baseline, final-output continuity, causal short-term memory, and later marker/M2G research.
Staleness trigger: refresh after the fresh/non-fresh continuity repair is tested, the pure-AI regression is rerun, or W3-W5 status changes.

## Current Objective

Preserve the validated fixed-shape Vision optimization and accepted control baseline;
repair fresh/non-fresh final-output continuity using the August 7 zero-right-stick
clip, then resume W3/W4 and implement W5 in shadow before new actuation authority.

## Current State

- Production chain remains Vision/selector -> TargetCoordinator -> TargetPlan -> ADS or BodyLock -> AimDynamicsShaper -> VectorIntentFuser -> ADS brake -> fixed recoil feed-forward -> ViGEm.
- Protected rollback source is commit `5d2f3be`; accepted runtime/config SHA begins `872F6FFF`. Pre-Vision-optimization and accepted CUDA Graph runtime backups remain under `artifacts/runtime-backups/`.
- The accepted fixed-shape CUDA Graph candidate SHA begins `57A78F84`; matched live A/B improved active result rate `99.2 -> 127.8 Hz` and source-present-to-ViGEm P50/P95 `13.05/20.63 -> 8.57/16.68 ms` without higher matched GPU power. It does not prove stable 160 results/s.
- **New user/runtime evidence:** with detailed telemetry disabled, a 180 Hz game can now appear to sustain approximately 180 Vision results/s. In logged session `20260805T194515Z_35884_1`, the user identifies the game as 240 Hz before clock minute 49 and 180 Hz afterward; ADS-only analysis measured approximately `135.5 -> 160.5` submitted Vision frames/s, accumulated-frame `>1` incidence `40.5% -> 8.4%`, and source-present-to-ViGEm P50/P95 `10.10/14.06 -> 8.70/12.41 ms`. Treat the no-log 180 Hz figure as user-confirmed runtime observation and the lower logged figure as telemetry-on evidence, not as a contradiction.
- W3's block matcher is a latest-only CPU worker, not a 2 ms inline controller call. In the 180 Hz game section its ADS compute P50/P95 was `1.98/2.21 ms`, feedback take-age P50/P95 `3.15/6.20 ms`, with no pending-frame replacement in usable ADS episodes. However, the Vision thread still performs grayscale CUDA staging, D2H readback and `cudaStreamSynchronize` before publishing; that serial tap is not separately timed in telemetry and consumes part of the 5.56 ms 180 Hz budget.
- A lightweight performance summary is now implemented independently of detailed telemetry. `[runtime.performance]` accumulates fixed 0.25 ms histograms on the hot path and writes one compact background JSONL window (default 5 s) with controller/output/Vision Hz, separately labelled active/idle accumulation pressure, end-to-end latency and separate W3 staging/compute. Release benchmark measured about `30 ns/tick`; records are test-bounded below 4 KiB. The local normal-play config enables it while `[runtime.telemetry]` remains disabled.
- The fresh-clamp/non-fresh-rebound command discontinuity remains proven and unresolved. Do not add smoothing, another hold/brake, or new actuation authority around it.
- **User-confirmed:** the August 7 smoothness clip used no right-stick input; right-stick camera motion was pure AI (`M=0`). Background-only analysis found repeated same-direction speed losses while DVR cadence was stable and decoded frames were unique.
- **Inferred/open:** the visual pulse shape is compatible with the known continuity defect and missing causal work accounting, but the run had detailed telemetry disabled and cannot assign a video frame to a controller branch.
- **Accepted target-first rule:** solve one final target-relative `T`; raw manual is evidence, not protected output. It may be reduced, cancelled or ignored to keep targetX/Y correct. `T = M + AI` is diagnostic accounting only.
- W3/W4 remain shadow-only and unpromotable; W5 has no `scheduled -> in-flight -> realized` ledger and no 150-200 ms memory authority.
- **User-requested future direction:** improve head-glitch/fence tracking through the enemy yellow marker and learn a session-local mapping from reliable target scale to marker-to-head/aim offset.
- **Proposed, not implemented:** learn only from same-frame strong direct target observations paired unambiguously with the marker; use the marker first for identity/ROI continuity, then consider a short-lived low-authority pseudo-observation with explicit uncertainty.
- **Post-W6 research direction:** survey maintained open-source mouse-to-gamepad implementations, reuse mature Raw Input/resampling/virtual-controller pieces, and replace open-loop mapping with the W4/W5 target-first solver.

## Next Action

Implement and review a bounded fresh/non-fresh continuity repair inside the
existing final-output owner. Add an event-triggered low-overhead trace for
`D/P/R/M/T`, freshness, identity and lifecycle boundaries, then rerun the same
pure-AI target-range scenario before resuming W3/W4/W5. When W3 resumes, first
instrument and remove or overlap its serial grayscale/readback synchronization;
W5 may consume timestamped realized feedback later but must never wait for it.

## Blockers

- The open fresh/non-fresh output discontinuity blocks promotion of new assist authority.
- The pure-AI clip has no controller trace, so it proves visible non-smoothness but not which internal field caused each pulse.
- W3 live validity remains too low for authority, W4 lacks a stable response curve, and W5 is absent.
- W3 matcher cost is known, but its serial Vision-thread staging cost and whole-pipeline interference at 180 Hz are not separately measured.
- Current yellow-cue association/scale evidence is unaudited; marker-only tracking must not self-train or become a second owner.
- HWiNFO samples do not measure wall power or sub-sample transients; PSU conclusions retain that limitation.

## Active Questions

- Does the unresolved final-output rebound still appear after a bounded continuity repair?
- After continuity repair, are residual pure-AI pulses caused by target demand `D`, missing pending work `P`, or the game response model?
- How should stable micro input update the target-internal aim point without preserving raw manual force?
- Can marker identity/scale remain unambiguous enough for later ROI-only continuation?

## Relevant Decisions

- [Adopt fixed-shape CUDA Graph Vision inference](decisions/DEC-2026-08-05-001-adopt-fixed-shape-cuda-graph-vision.md)
- [Use yellow cue as a short continuation hold](decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md)
- [Protect live baseline and defer W5](decisions/DEC-2026-08-03-001-protect-live-baseline-defer-w5.md)
- [Predictive manual/AI control envelope](decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md)
- [Target-count-aware manual exit authority](decisions/DEC-2026-08-03-002-target-count-aware-manual-exit-authority.md)
- [Solve target-first final output with causal work accounting](decisions/DEC-2026-08-07-001-target-first-final-output.md)

## Files To Read First

1. [Target-first final-output decision](decisions/DEC-2026-08-07-001-target-first-final-output.md)
2. [Current project state](../docs/project/CURRENT_STATE.md)
3. [CUDA Graph Vision decision](decisions/DEC-2026-08-05-001-adopt-fixed-shape-cuda-graph-vision.md)
4. [Compact session log](session-log.md)

## Do Not Reopen Unless Needed

- Do not attribute the Vision gain to more GPU power; matched hardware evidence contradicts that explanation.
- Do not collapse the Vision evidence into one headline rate: the matched earlier A/B measured `127.8 Hz`, the later telemetry-on 180 Hz game section measured about `160.5 Hz`, and the user reports about `180 Hz` with telemetry disabled. Preserve the workload/logging conditions with every rate.
- Do not use current occlusion-truncated box size as a learned distance/scale label.
- Do not let marker-derived positions train the marker mapping or bypass selector/coordinator identity ownership.
- Do not restore additive manual-plus-AI forces, manual preservation floors or another final-output owner. `T = M + AI` is not an implementation rule.
- Do not attribute the August 7 pure-AI clip to right-stick irregularity; the user confirmed `M=0`.
- Do not call PendingMotion/rollout shadow W5 or use generic smoothing as causal memory.
- Keep raw telemetry, external hardware logs, screenshots, binaries and personal paths out of project context and ordinary source commits.
- Use the lightweight performance summary for throughput/latency A/B. Enable detailed telemetry only for a bounded causal diagnosis; do not compare a telemetry-heavy run with a summary-only run as though logging conditions matched.

## Notes

- The validated executable was built before the source-protection commit and reports the earlier build-time Git identity; its full executable SHA-256 is the runtime identity for the recorded A/B.
- Raw benchmark/runtime artifacts and executable backups remain local and intentionally untracked.
