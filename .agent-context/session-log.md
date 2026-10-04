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

## 2026-09-26 — Gamepad multiplayer improvement checkpoint

- **User-confirmed:** ordinary multiplayer/normal aim assist no longer showed felt jitter or oscillation after the requested build; user authorized recording, removing unimportant generated byproducts and committing.
- Retained changes: 15–30% intent/authority curve without raw deadzone, final DS4 delivery receipts and failure boundaries for camera work, detector confidence separate from ranking, and accumulation of short source intervals for high-rate observation/learning. Arbitration implementation moved out of the public header. Rejected calibration/prediction experiments were restored before the tested build.
- **Verification:** 410 product tests pass; high-rate RED→GREEN contract complete; 864 fixed opportunities show no new protected regressions against the preceding continuous-intent worktree, and 1,944 development random cases match. Existing synthetic mismatch RED remains failing; no global optimization acceptance.
- **Bounded audit:** exact runtime/config hashes match the prepared build. Two intact rotations cover 658 seconds; 61,766 unique controller samples and 142,981 delivered samples contain no delivery failure, nonfinite axes or output overflow. Event-ring repeats/out-of-order emission were handled by exact producer sample sequence, with no ambiguous cross-stream tick join.
- **Inferred/open:** one 217 ms reversal signature and one ADS-extension output jump remain saved observations; no independent motion truth proves their cause. Last log rotation has a truncated row, session did not cleanly close, game refresh/hardware profile and matched baseline are unavailable. Audit remains INSUFFICIENT_EVIDENCE for full live acceptance. Strong-AA modes are not covered by the user's normal-multiplayer confirmation.
- Removed only a superseded early scan CSV and a generated probe object (87.70 MiB); latest raw logs, RED/GREEN evidence, final matrix reports and runtime backup retained. Unrelated workspace work excluded from the commit.
- Record: `docs/benchmarks/CONTROLLER_MULTIPLAYER_CHECKPOINT_20260926.md`; source/build/audit hashes remain in its referenced local artifacts.
- SyncSet reviewer: `accept_draft`; direct user authorization covers recording, prior handoff archived, no raw telemetry/personal machine paths copied into context, no new global acceptance decision. No subagents used.


## 2026-10-04 — Isolated C++ cognition refactor completed

- User authorized completion in a new worktree without repeated questions. Branch `codex/cognition-refactor-20261004`; inherited source/doc snapshot `1251273` preserves original concurrent changes. Original workspace untouched; output media excluded from the snapshot.
- Read project-cognition method and existing coordination review; updated existing project model instead of introducing a new knowledge framework. Concentrated IPC implementation/reload transitions, Loop control versus diagnostics, shared telemetry shapes and Fusion conversion; removed repeated derived state and field mapping. Kept reasonable controller/Vision/mouse ownership boundaries and actual Python dependencies.
- RED-to-GREEN boundary fixes: malformed scalar configuration is rejected rather than silently defaulted/prefix-parsed; output disabled does not construct a virtual gamepad. No production selector/control algorithm change; one obsolete Python selector expectation aligned with the existing native flat-person identity contract.
- Fresh Release full build, CTest 50/50 groups and affected Python tests 134/134 pass. Actual output-disabled TensorRT runtime completes control-value and vision-policy reloads via Python IPC/Loop and named-event stop, with current telemetry serialized. Device receiver, gameplay, Fusion window and independent review remain unverified.
- Scope/details/source/verification: `docs/project/CURRENT_STATE.md`. Old SHA256 validation and benchmark acceptance framework remain retired; retained simulation tests do not constitute a release verdict.
- SyncSet reviewer: accept_draft (self-review); user's current one-pass authorization covers this worktree handoff/milestone update. No decision rewrite, raw telemetry, personal captures or secrets included. No subagents or cross-chat confirmation claimed.


## 2026-10-04 — Test cleanup and C++-only authorization

- User authorized deletion/consolidation of old tests, emphasizing pre-August scenarios. Removed 74 Python test modules, 29 native historical incident drivers and 14 test-only fixtures; removed 13 source-shape assertions and migrated current product authority/manual-correction assertions into native base contracts.
- Release all-target build and CTest 50/50 groups pass. Remaining Python full collection was 270 pass/1 skip before removing 13 static assertions; affected startup 18 pass and final default basic set 123 pass, full collection now 258. Old detailed fallback/research/incident coverage is intentionally retired, not equivalent coverage.
- Latest user explicitly authorizes eliminating Python runtime/build/tool participation and dependencies, with useful capabilities implemented in C++. This supersedes earlier retention boundaries. Migration is in progress; GUI/config/start-stop/hot-reload/learning display are selected useful capabilities.
- SyncSet self-review accept_draft under continuing one-pass authorization: current scope and completed checks only; no secrets/raw logs/decision rewrites. Original workspace remains untouched.

## 2026-10-04 — C++-only migration completed

- User explicitly authorized retirement of every Python path and dependency. Removed remaining Python runtime, GUI, tests, training, research/analysis tools and pybind extension. Selected desktop config edit/validate/save, verified start/stop, reload/state/learning and logs now use native C++; old graphical curve editor and preset library are retired, user assets and native curve semantics preserved.
- Native DesktopSession owns configuration bytes/conflict detection and PID+creation-time+normalized executable identity. Win32 window delegates commands; existing IPC ABI extracted into one shared header. Suspended child startup records ownership before running; shutdown uses the runtime event, no process-name force kill. SDK SDL/ViGEm inputs now come from native runtime_deps or explicit build parameters, not site-packages.
- Fresh Python-free CMake configure and full Release build pass. Final CTest 51/51 groups pass including numerical simulation and hidden window. Real output-disabled native TensorRT start/control reload/vision-policy reload/stop pass; learning preserved then cleared at appropriate commits. Standalone native vision completes three real captured/inferred frames. PowerShell AST and background start/stop/Fusion previews pass.
- Malformed config-line RED regression corrected at native loader; Windows executable-path ownership mismatch reproduced and corrected at native session normalization. No controller algorithm change. Native config editor uses current loader subset; general TOML editing is not promised.
- Physical input/output/gameplay, Canvas exclusion and full manual GUI use remain unverified. No ONNX available for real trtexec export. Automatic approval rejected recursive ignored Python/pytest cache cleanup with blocked by policy; no workaround attempted. These historical ignored outputs do not participate in current paths.
- Source model and docs updated in target worktree only. SyncSet self-review accept_draft under ongoing one-pass authorization; original workspace untouched, no decision rewrites, no external coordination confirmation claimed.
