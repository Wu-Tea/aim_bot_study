# Agent Handoff

Last updated: 2026-08-12
Active scope: default native C++ Vision-to-gamepad runtime.
Staleness trigger: refresh after matched live validation, a new production-chain
regression, or any proposal to restore predictive authority.

## Current Objective

Validate the completed Controller/auto-mark candidate in one matched Black Ops 7
session. The bounded refactor and deterministic GREEN proof are complete;
screen-space D/R geometry and live calibration acceptance remain.

Runtime: `native/vision_native/build/Release/cod_native_runtime.exe`
SHA-256: `51F1A3ACDD204D1BA064DFA873A64CAB9DBA2C92E76BB849252BDDB5B1071861`
L3 mark requests use configurable `l3_cooldown_ms` (currently 1000); LT is not
subject to this cooldown. Codex did not launch the runtime.

## Production Chain And Invariants

```text
fresh unique Vision result -> selector owns I/R -> delivery gate
-> TargetCoordinator owns D/TargetPlan -> ADS or BodyLock
-> AimDynamicsShaper -> AssistControlStateMachine owns final T
-> AutoFire safety -> recoil feed-forward -> ViGEm
```

- Repeated controller ticks are not fresh Vision observations. Fresh no-target
  releases generic aim authority; cue continuation is bounded and aim-only.
- Selector admits direct class-0 people, rejects green friendlies and known
  corpse/marker patterns, retains identity through brief dropout internally but
  never publishes stale coordinates, and performs explicit handover.
- `AssistControlStateMachine` is the only final-output owner. Each axis uses
  desired total `T` after native manual `M`: helpful manual fills the same work,
  AI fills only the residual, and opposing AI may damp but never reverse manual.
  Ordinary/down/firing-down damping ceilings are 35%/10%/0%.
- Once a selector-owned person is admitted, ADS uses full configured authority
  regardless of cue, visibility, reliability or distance. A confirmed new I
  gets fresh ADS acquisition even while LT remains held. BodyLock stays
  evidence-scaled.
- D is the intended hittable point inside R. Firing-down moves D inside R and
  cannot request downward handover; recoil stays a later feed-forward stage.
- L3/LT mark is a final-plan transaction: two consecutive fresh, same-generation
  direct-person plans with current enemy cue and crosshair inside valid R emit
  one 50 ms D-pad Up. Request lifetime is 250 ms. L3 can wake Vision but grants
  no aim authority; physical D-pad Up always passes through.

## Verification

- Full candidate Release build: PASS.
- Official Release CTest: 49/49; direct candidate replay: 49/49.
- Product-contract known-bad is RED; candidate is GREEN on all seven oracles.
- ADS no-cue/cue authority: `0.228/0.950 -> 1.000/1.000`.
- Firing-down D: `256 -> 256 px` became `256 -> 312 px`; output is not opposed.
- Telemetry schema 17 record: 1848 bytes, below the 2576-byte ceiling.
- Source snapshot: 194 production/config files, Git-style hash
  `6f3addd009ca38ac0d996ffbce77742bcf7ba786`.
- Fixed publication of completed ADS as BodyLock with ADS still active.
- Aimlab wrong-person strong ADS is now a hard failure (`final_score=0`).
- Evidence: `artifacts/regressions/controller-v1-product-contract-20260812/`.

Aimlab was not expanded into a game simulator. GREEN proves owner/state defects,
not gameplay geometry, cue visibility through cover, or COD calibration.

## Evidence Behind The Refactor

The 2026-08-12 18:12 replays were joined to telemetry by save time minus
duration plus ADS/fire/candidate fingerprints; the join is event-level:

- cue-less people were admitted at roughly 83 px and 109 px, but cue still
  effectively gated delivered ADS force;
- one incident had 27 selected-target ADS samples with zero delivered AI;
- a two-person incident crossed selector generations 41-45 in about 711 ms;
- firing-down reached final output but was excluded from D correction, while
  whole-vector alignment could attenuate Y because X agreed with AI;
- auto-mark actuated before an accepted controller target existed.

They justify full-authority ADS, per-axis fusion, protected firing-down D,
explicit identity ownership, and final-plan marking—not Body/Pose or a new owner.

## Next Action

Run one session with the exact candidate/hash. Test Controller with mark off,
then enable mark and test L3/LT separately. Cover: small ADS correction,
held-LT urgent transfer, firing-down recoil, close BodyLock, head-glitch D/R,
green friendly, corpse, off-axis person, and physical D-pad passthrough.

On failure, join schema-17 fields to the exact video event and create one RED
fixture. Do not weaken ADS, globally raise BodyLock, add an output owner, or add
Body/Pose until evidence isolates a selector/geometry limitation.

## Do Not Reopen Without New Evidence

- additive manual-plus-AI output, alternate final-output owners, projection,
  generic coasting, output carry/brake, or retired config aliases;
- cue-derived self-training/fire authority or repeated-tick freshness;
- unmatched benchmark/live comparisons;
- Python fallback behavior as evidence about the default native runtime.
