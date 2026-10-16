# Agent Handoff

Last updated: 2026-10-16
Active scope: clean rebuilt from the verified DEV baseline; product and development worktrees are separated.

## Current state

- User changed the work order: commit DEV uncommitted work first, then sync clean optimizations back to DEV. The checkpoint is `6496f84` (long capture, React toggle, Vision profiling and diagnostics). Capture recordings/logs under `output/` remain local and untracked.
- Optimization source is the deletion-before archive `5eb02ff` on `codex/runtime-clean-validation-archive-20261016`, compared with the original DEV source snapshot. All selected existing source/test baselines matched DEV. No clean-only packaging cuts or orphan historical files were imported.
- DEV retains React/WebView2, mouse runtime, tests and offline tools. Solvers no longer take unused manual/time inputs; arbitration, target/ADS state, observation expiry, output continuity and reload rules use their existing owners with duplicate state removed. Three DEV-only mouse tests were adapted to the reduced signatures without changing assertions.
- Current behavior and state/interaction documentation: [configuration and checks](../docs/project/CONFIGURATION_AND_CHECKS.md), [current project model](../docs/project/CURRENT_STATE.md).
- User authorized recreating clean from current DEV while retaining full React/WebView2 GUI, native runtime, fixed recoil and dependencies; tests/experiments stay in DEV. New clean is `ba679ce`, tagged `codex/clean-baseline-20261016`, derived from `aa0f918`. GUI is not Fusion; this distinction is now explicit in clean AGENTS/README.
- Old clean, including its unfinished trim, is archived as `ca0ea11` on `codex/runtime-clean-archive-20261016`; its separate archive worktree preserves source and 2,394 copied/verified local files. The new clean keeps the original worktree path.
- Clean has 232 tracked product files; 203 retained native/GUI source and resource files match DEV. Runtime/GUI algorithms were not changed by packaging. Four local GUI profiles and their required engines are present; models/configs/DLLs stay ignored.

## Verification and limits

- Clean migration: native/frontend builds pass; external DEV fixtures linked against clean libraries pass 448 native cases; 80 desktop and 10 capture cases pass against clean modules. Seven replay tables match DEV exactly. Real WebView2 start → actual engine initialization → fixed-recoil hot reload Applied → stop passes with output/haptics disabled. Actual Fusion attach/close passes and closing Fusion preserves runtime. Evidence and temporary validation harness: `runs/clean-recreate-20261016/` in DEV (ignored).
- Before: Release runtime/base/functional/Fusion build and 50/50 CTest groups pass. After: full default Release build, 51/51 CTest groups, frontend build, 80 desktop tests, 10 capture tests and real WebView2 smoke pass. The user stopped the running runtime; the default `native/build/Release` executable was then rebuilt successfully.
- Original DEV replay fixture linked against the updated controller gives identical records for two independent seeds, 192 short/long scenarios and 676,544 ticks. Detailed clean replay records also match for cue gaps, revocation, delivery history, learning reload and policy commit. Policy commit replay models scheduling; it is not direct concurrent RuntimeLoop integration coverage.
- Runtime loaded the real engine and completed a bounded 1,000-tick smoke with output and haptics disabled. Actual gameplay feel and live device reconnect are not established by these checks. DSE pulse feel was accepted previously; automatic live-game notification acceptance remains open.
- Local build/test evidence is in the session's `dev-sync` scratch directory. No raw capture or validation artifacts are committed. No remote push requested.

## Resume and boundaries

- Next action: use the new clean baseline; branch temporary work from it for major changes. Keep tests and experiment history in DEV, and apply product changes only after comparison against clean.
- Preserve source-selected chest reference, per-axis 15–30% manual authority, raw passthrough, identity/handover and full controller lockstep. Preserve user models/profiles/configuration. Do not restore retired provenance gates or Tk GUI.
- No migration blocker remains. Live-game feel and actual physical-device output/reconnect acceptance remain separate from output-disabled smoke and simulations.
