# Agent Handoff

Last updated: 2026-08-05
Active scope: native C++ FPS gamepad runtime; validated Vision throughput optimization, protected control baseline, target continuity, and future marker-assisted occlusion tracking.
Staleness trigger: refresh after the yellow-cue path is audited, the fresh/non-fresh continuity defect is repaired, or the CUDA Graph runtime/engine/driver contract changes.

## Current Objective

Preserve the validated fixed-shape Vision optimization and the accepted control
baseline. When work resumes, audit the current yellow-cue observation path and
design a shadow-only target-scale/marker-offset learner before granting any new
tracking or actuation authority.

## Current State

- Production chain remains Vision/selector -> TargetCoordinator -> TargetPlan -> ADS or BodyLock -> AimDynamicsShaper -> VectorIntentFuser -> ADS brake -> fixed recoil feed-forward -> ViGEm.
- Protected August 3 rollback source is commit `5d2f3be`; its accepted executable/config SHA begins `872F6FFF` and remains under `artifacts/runtime-backups/`.
- The pre-optimization runtime is backed up at `artifacts/runtime-backups/pre-vision-opt-20260805-69E8624C/`; SHA-256 begins `69E8624C`.
- The installed and live-tested Vision candidate SHA-256 is `57A78F843A7CDB4AA474B7F6968B7B83C7DAFD15101B1210272800692E43AC25`; the normal background launcher resolves to this build output.
- The Vision candidate binds fixed TensorRT addresses once, uses a high-priority non-blocking CUDA stream, and replays only the fixed TensorRT inference segment through CUDA Graph. Dynamic DXGI capture, ROI/preprocess and result handling remain outside the graph.
- Offline 1,500-image A/B retained identical detections and float outputs. Wall P50/P95 improved `3.343/6.078 -> 1.207/2.432 ms`; enqueue CPU P50 improved `2.894 -> 0.083 ms`; GPU-total P50/P95 improved `3.135/5.856 -> 1.015/2.213 ms`.
- Real 160 FPS A/B used sessions `20260805T120935Z_47940_1` and `20260805T125610Z_44588_1` with matching config and engine. Capture-to-result P50/P95 improved `8.08/13.41 -> 5.42/9.34 ms`; active effective result rate improved `99.2 -> 127.8 Hz`; source-present-to-ViGEm P50/P95 improved `13.05/20.63 -> 8.57/16.68 ms`.
- In the matched HWiNFO stable window, 4070S use/power was essentially unchanged (`77.54/84.94 W -> 77.26/83.77 W`), so the live gain did not come from raising GPU power. Power and thermal limit flags stayed inactive.
- The optimization materially improves 160 Hz budget compliance but does not prove stable 160 results/s; capture-to-result met 6.25 ms on about `70%` of active samples.
- The previously proven fresh-clamp/non-fresh-rebound command discontinuity remains unresolved. Do not expand marker-derived actuation authority while this output-continuity defect is open.
- W3/W4 remain shadow-only and unpromotable; W5 causal short-term memory is not implemented.
- **User-requested future direction:** improve head-glitch/fence tracking through the enemy yellow marker and learn a session-local mapping from reliable target scale to marker-to-head/aim offset.
- **Proposed, not implemented:** learn only from same-frame strong direct target observations paired unambiguously with the marker; use the marker first for identity/ROI continuity, then consider a short-lived low-authority pseudo-observation with explicit uncertainty.

## Next Action

At the next implementation session, read the existing yellow-cue detector,
association and continuation path and identify which same-frame direct target
scale, marker anchor, identity, FOV/viewport and truncation-quality fields are
already available. Propose a shadow-only observation schema and validation gate;
do not change control output in that first step.

## Blockers

- Current yellow-cue association and scale/truncation evidence have not yet been audited; a partial visible box cannot safely stand in for full target scale.
- Marker-only tracking must not self-train from marker-derived pseudo positions or become a second target/control owner.
- The open fresh/non-fresh output discontinuity blocks promotion of new assist authority.
- HWiNFO samples do not measure wall power or sub-sample transients; PSU conclusions retain that limitation.

## Active Questions

- Is marker-to-head vertical offset approximately proportional to stable full-body height, or does it require a monotonic scale/FOV/posture model?
- Can marker identity remain unambiguous with multiple enemies, UI cues and intermittent obstruction?
- What minimum independent scale coverage and residual bound should unlock ROI-only and later low-authority use?
- Does the unresolved final-output rebound still appear after a bounded continuity repair?

## Relevant Decisions

- [Adopt fixed-shape CUDA Graph Vision inference](decisions/DEC-2026-08-05-001-adopt-fixed-shape-cuda-graph-vision.md)
- [Use yellow cue as a short continuation hold](decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md)
- [Protect live baseline and defer W5](decisions/DEC-2026-08-03-001-protect-live-baseline-defer-w5.md)
- [Predictive manual/AI control envelope](decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md)
- [Target-count-aware manual exit authority](decisions/DEC-2026-08-03-002-target-count-aware-manual-exit-authority.md)

## Files To Read First

1. [CUDA Graph Vision decision](decisions/DEC-2026-08-05-001-adopt-fixed-shape-cuda-graph-vision.md)
2. [Current project state](../docs/project/CURRENT_STATE.md)
3. [Yellow-cue continuation decision](decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md)
4. [Compact session log](session-log.md)

## Do Not Reopen Unless Needed

- Do not attribute the Vision gain to more GPU power; matched hardware evidence contradicts that explanation.
- Do not call the current result stream stable 160 Hz; measured active mean is about 128 Hz.
- Do not use current occlusion-truncated box size as a learned distance/scale label.
- Do not let marker-derived positions train the marker mapping or bypass selector/coordinator identity ownership.
- Do not restore additive manual-plus-AI forces or add another final-output owner.
- Keep raw telemetry, external hardware logs, screenshots, binaries and personal paths out of project context and ordinary source commits.

## Notes

- The validated executable was built before the source-protection commit and reports the earlier build-time Git identity; its full executable SHA-256 is the runtime identity for the recorded A/B.
- Raw benchmark/runtime artifacts and executable backups remain local and intentionally untracked.
