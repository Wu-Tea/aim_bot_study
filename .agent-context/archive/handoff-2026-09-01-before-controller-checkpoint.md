# Agent Handoff

Last updated: 2026-09-01
Active scope: Fusion recovery is delivered on both branches; BodyLock live acceptance remains open.
Staleness trigger: refresh after the first real MW4 Fusion display transition or
the next live BodyLock candidate session.

## Current Objective

Live-confirm that the clean-branch Fusion launcher survives normal MW4 startup,
fullscreen/alt-tab changes and display transitions without weakening exclusion.

## Current State

- Both failing runs resolved paths inside the clean worktree, passed DXGI
  preflight, connected IPC and entered `running`; repository absolute paths
  were not the failure owner.
- `fusion_canvas.cpp` treated either `WM_DISPLAYCHANGE` or
  `WM_DWMCOMPOSITIONCHANGED` as terminal proof loss. The old combined log
  cannot identify which notification occurred.
- Canvas now hides, records the exact reason, refreshes virtual-screen geometry,
  reapplies/readbacks `WDA_EXCLUDEFROMCAPTURE`, reruns the production
  `DxgiRoiCapture` probe and resumes only after the full proof passes.
- Failed, ambiguous or repeatedly invalidated verification remains fail-closed.
  The recovery probe has an independent window procedure and renderer resize
  reports failure while releasing replaced graphics references.
- The launcher reuses its current PowerShell host and checks Canvas liveness
  immediately before publishing state.
- The lifecycle fixture was observed RED then GREEN. Injected display and DWM
  notifications both revalidated while Canvas/native remained alive.
- Verification: clean full Release build and `11/11` contracts PASS; dev
  Fusion build, `26/26` CTest and DXGI preflight PASS.
- Commits: `bd462f3` on `dev`, `62e3604` on
  `codex/runtime-clean`; repaired blobs are identical.
- The August BodyLock total-demand candidate remains GREEN offline but lacks
  matched live firing/FOV/noise acceptance.

## Next Action

Run `scripts/launch/gamepad_fusion_background_start.vbs` from the clean
worktree in the normal MW4 workflow, exercise one display transition, and read
the newest exact-reason Fusion log only if Canvas does not remain running.

## Blockers

- No known build or deterministic Fusion regression blocker remains.
- Real MW4 presentation-mode and physical topology resize acceptance are pending.
- BodyLock live acceptance is separately pending.

## Active Questions

- Whether repeated real-game display notifications keep the recovery probe
  acceptable in the user's exact presentation mode.
- Whether physical monitor resize exposes a renderer/device recreation failure.
- Whether BodyLock remains smooth under recoil, FOV and detector geometry noise.

## Relevant Decisions

- `decisions/DEC-2026-06-25-001-performance-first-fusion-canvas.md`
- `decisions/DEC-2026-08-07-001-target-first-final-output.md`
- `decisions/DEC-2026-08-11-001-incident-first-gameplay-validation.md`
- `decisions/DEC-2026-08-17-001-retire-direct-controller-experiment.md`

## Files To Read First

- newest `runs/fusion_canvas/background/fusion_canvas.*.log`
- `native/overlay_canvas/fusion_canvas.cpp`
- `native/overlay_canvas/fusion_overlay_contract_tests.cpp`
- `scripts/launch/gamepad_fusion_background_start.ps1`
- `docs/project/FUSION_ENEMY_VISIBILITY_OVERLAY_V2_PLAN_20260830.md`

## Do Not Reopen Unless Needed

- repository absolute paths without new path-resolution evidence;
- rendering while isolation is unproven or replacing DXGI proof with affinity alone;
- unrelated ADS, BodyLock, Vision, AutoFire or recoil tuning.

## Notes

Preserve unrelated work, including the untracked MW4 research document; it is outside both commits.
