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
