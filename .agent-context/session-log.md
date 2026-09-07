# Agent Session Log

Last compacted: 2026-09-01
Scope: native C++ FPS runtime, Vision/Controller ownership, Fusion Canvas,
capture isolation, native gates and runtime operations.

## How To Use This File

- Read the newest checkpoint and `handoff.md` first.
- August 3-18 detail: [pre-September archive](archive/session-log-2026-08-03-to-2026-08-18-pre-20260901-compaction.md).
- August 1-3 detail: [August archive](archive/session-log-2026-08-01-to-2026-08-03-pre-20260803-compaction.md).
- July 23-31 detail: [July archive](archive/session-log-2026-07-23-to-2026-07-31-pre-20260801-compaction.md).
- Earlier history: [pre-July-20 context](archive/context-through-2026-07-20-pre-compaction.md) and `session-log-full.md`.
- **User-confirmed**, **Repository evidence** and **Inferred/open** retain their
  literal meanings; do not promote an inference into a fact.

## 2026-09-01 - Native-Only Branch and Fusion Recovery

- **User-confirmed scope:** create a clean native-runtime branch, keep local
  model/config untracked, compile it, repair Fusion on clean and dev, then sync
  context and commit.
- `codex/runtime-clean` tracks only the C++ runtime, Fusion, native contracts,
  launch/build scripts and current required docs. Machine-local assets remain ignored.
- Both failed Canvas runs resolved clean-worktree paths, passed the production
  DXGI preflight, connected IPC and entered `running`; absolute repository
  paths were not the owner.
- **Root cause:** display and DWM notifications shared one invalidation flag and
  were treated as terminal proof loss instead of a request to hide and revalidate.
- **Fix:** Canvas now enters `RevalidationPending`, hides, logs the exact
  reason, refreshes geometry, reapplies/readbacks exclusion, reruns the
  production `DxgiRoiCapture` probe and resumes only on full proof. Failure,
  ambiguity or another change during proof remains terminal fail-closed.
- The recovery probe cannot post `WM_QUIT` to production; renderer resize
  reports failure and releases replaced graphics references. The launcher uses
  its current PowerShell host and verifies liveness before publishing state.
- **RED/GREEN:** the new lifecycle fixture first failed, then passed. Injected
  `WM_DISPLAYCHANGE` and `WM_DWMCOMPOSITIONCHANGED` each completed real DXGI
  revalidation while Canvas/native stayed alive.
- **Verification:** clean full Release build and `11/11` suites PASS; dev
  Fusion build, `26/26` CTest and DXGI preflight PASS. Test processes were stopped.
- Commits: clean creation `e7d8840`; repairs `62e3604`
  (`codex/runtime-clean`) and `bd462f3` (`dev`). Repaired blobs are identical.
- **Inferred/open:** the old combined log cannot prove which notification caused
  the original exits. Real MW4 mode changes and physical topology resize remain live gates.
- No subagents were used. Raw logs, PIDs, SDK paths, binaries, secrets, local
  asset details and unrelated research were excluded.
- SyncSet `sync-20260901-001`, reviewer `accept_draft`: updated handoff and the
  accepted Fusion decision; archived the prior 563-line log before compaction.
- Follow-up: run the clean launcher in normal MW4 mode and exercise one display transition.

## Current Active Checkpoints

- **Fusion:** deterministic recovery is GREEN; real MW4 presentation changes and
  physical monitor resize remain live acceptance boundaries. Never weaken capture
  isolation to improve liveness.
- **BodyLock:** the August 18 capture-aligned observer moved the frozen step from
  no response within 900 ms and `21.040884 px` growth to 23 ms and
  `2.698075 px`, with zero recovery overshoot. Live recoil/FOV/noise feel and
  the high online response estimate remain unverified.

## Durable Milestone Index

- 2026-06-25: accepted performance-first, visual-only Fusion with no gameplay authority.
- 2026-08-01 to 03: established one final output and protected the live baseline.
- 2026-08-05: accepted fixed-shape TensorRT/CUDA Graph throughput optimization.
- 2026-08-07: clarified target-first `T`; manual is evidence, not a protected force.
- 2026-08-10: retired the low-rate legacy stack and duplicate owners.
- 2026-08-11: implemented I/R/D/T ownership and incident-first validation.
- 2026-08-12: granted full ADS authority after admission and completed auto-mark.
- 2026-08-16: bounded latency test found no material controller-internal queue.
- 2026-08-17: repaired BodyLock axis reversal and retired Direct/PID production.
- 2026-08-18: implemented capture-aligned BodyLock sustaining demand; live gate open.

## Current Architecture and Maintenance

```text
Vision -> selector -> TargetCoordinator -> TargetPlan
       -> ADS acquisition OR BodyLock follow
       -> AimDynamicsShaper -> AssistControlStateMachine
       -> AutoFire -> independent recoil -> virtual gamepad

Vision -> latest-only Fusion channel -> visual-only Canvas
                                      -> fail-closed DXGI exclusion lifecycle
```

One owner per identity, lifecycle, state and final-output decision. Fusion never
owns gameplay control. Fresh Vision owns position. Aggregate benchmark scores do
not replace incident/product gates. Keep secrets, personal media, raw telemetry
and machine-local runtime assets out of project context.

## 2026-09-07 — Bounded target addressing

- User authorized geometry-only target addressing, followed by a scoped commit and documentation; another session is independently optimizing Vision.
- DesiredPointReducer now chooses a bounded upper point once on fresh ADS admission and retains its normalized position across fresh/cue updates and ADS→BodyLock. Source association and motion geometry remain Vision-owned.
- Standard-box selection is bounded to approximately 40%→36.4% height, with a central approach corridor and two arrival radii reserved for braking. Manual correction wins; replacement resets offsets.
- Frozen RED→GREEN fixture: extra downward demand 7.2→0 px. Final 10 Base/Feature suites, 365 cases PASS; current workspace launcher runtime rebuilt. An intermediate telemetry rotation test failed intermittently and passed on the final run; its cause is unresolved.
- Live feel and silhouette correctness remain unverified. No claim of neck recognition, FPS improvement, or live acceptance.
- Implementation and reproduction: `docs/project/TARGET_ADDRESSING_GEOMETRY_20260907.md`; commit subject: `fix(controller): retain bounded approach-selected target points`.
- Scope excludes Vision source/configuration, its experiment document, models, shared build changes, and other worktrees. No handoff or existing decision was rewritten.
- SyncSet reviewer: `accept_draft`; user-authorized documentation, factual results separated from unverified live outcomes; raw media, telemetry and secrets excluded.
