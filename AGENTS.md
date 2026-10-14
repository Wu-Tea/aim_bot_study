# Workspace AimLab rules

## Default controller optimization boundaries

User-confirmed on 2026-09-26; apply to controller fixes and optimization:

- Prefer correcting the algorithm and ownership logic; preserve or reduce
  logical gates instead of layering symptom-specific switches and delays.
- Do not sacrifice target search, acquisition, identity, or handover performance.
- Minimize overshoot and repeated oscillation while preserving manual authority,
  zero-deadzone native passthrough, and the user-defined AI intent curve.
- The user replaced the hard 25% AI intent threshold with a smooth 15%-30%
  authority band: no manual intent weight at/below 15%, full weight at/above
  30%, and continuous interpolation between. Raw passthrough stays untouched.
  Legacy 25% admission/full-direction tests are superseded; explicit exit and
  strong manual authority remain protected.
- Validate with many deterministic randomized short and long scenarios and
  independent validation seeds. Passing a few selected examples is insufficient.
- Keep scenario inputs and simulation assumptions explicit; report per-case
  behavior and fidelity gaps. Simulation success is not live-game acceptance.

## Controller intent and motion ownership

User-confirmed on 2026-10-14; keep this architectural boundary stable:

- Interpret manual onset, reversal, release, correction purpose and authority
  independently on X and Y. A change on one axis must not reclassify or revoke
  the other axis's held input. Do not alternate between whole-stick attenuation
  and axis ownership as a tuning technique.
- Estimate target/camera motion, solve tracking and enforce the final physical
  output envelope in two dimensions. Per-axis authority supports arbitrary
  diagonal corrections; it does not mean two unrelated target trackers.
- Preserve raw passthrough, explicit manual exit, ADS Snap, Body Lock, existing
  manual authority weights, Profile values and force limits unless separately
  authorized. Opposing AI does not by itself prove the user is wrong.
- User superseded aim-point retention on 2026-10-14: gamepad manual input
  affects actual output through per-axis authority, never the AI anatomical
  reference. Keep the source-selected chest estimate during hold and release;
  do not integrate either light or strong input into a new aim point.
- Preserve sustained manual exit/handover gesture recognition separately from
  anatomical point ownership. Mouse/legacy semantics are outside this change.
- Do not add chest-to-head switching without reliable anatomical visibility
  evidence. Current body-box geometry is an estimate, not a visibility label.
- Cover source-point invariance, per-axis manual authority, explicit exit,
  gaps and release in regressions and full-chain simulations. Simulation
  success is not live-game acceptance.

## Validation tooling cleanup

User-authorized on 2026-10-04: retire the old SHA256 provenance/checking and
benchmark acceptance framework. Numerical simulations and ordinary functional
unit tests remain available. Do not restore removed comparison gates, audit
skills, hash manifests, or release verdicts without a new user request.

Use simulations to inspect behavior and numerical invariants. Scores and
successful execution do not establish live-game acceptance. Preserve the fixed
target schedule (1575 ms slot + 50 ms gap by default), controller lifecycle,
manual authority, and production control algorithm during tooling maintenance.

The accepted runtime keeps the complete controller chain in lockstep. Retired
`ai_proposal_mode`, `ai_proposal_hz`, and `ai_proposal_update_stage` knobs remain
unknown/inert; deleting validation tooling does not authorize reintroducing them.

## Native runtime and current desktop GUI

User-confirmed on 2026-10-12: keep only the React/WebView2 desktop GUI launched
by `启动助手.vbs`; remove the old Tk GUI and its dedicated widgets/test tools.
This supersedes the October 4 requirement to retain dev's Tk workspace.
`python/desktop_app/gui.py` remains the thin launcher and command-action entry;
its filename does not mean the old GUI is retained. UI changes belong in
`python/desktop_app/web_ui/`, with `web_host.py` and `web_service.py` as the host
and bridge. Do not restore the Tk or excluded C++ GUI without a new request.
Python remains for the desktop host, configuration/curves, process management,
IPC and GUI checks. Native build/control/Vision stay Python-free; retired
Python fallback, training and analysis paths must not be restored from history.
Preserve user models, datasets, profiles and configuration.
