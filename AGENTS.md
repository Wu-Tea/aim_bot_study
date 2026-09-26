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
- Freeze scenarios and acceptance rules before candidate results; report per-case
  regressions and fidelity gaps. Simulation success is not live-game acceptance.

When the user asks to run, use, tune, sweep, optimize, or interpret AimLab or
Sustained AimLab, first read
`docs/benchmarks/AIMLAB_OPTIMIZATION_CONTRACT_V1_20260827.md` and treat it as
the normative optimization contract.

- Never optimize or recommend a candidate from additive score alone.
- Product constraints decide eligibility; acquisition, tracking, and smoothness
  points rank only candidates that are already eligible.
- Use matched baseline/candidate covariates and run
  `scripts/verify/compare_sustained_aimlab.ps1`. Do not waive a protected
  regression because another metric or the total score improved.
- Run the relevant native product/regression gates for ADS, BodyLock, identity,
  manual authority, lifecycle, AutoFire, recoil, and output safety. A metric in
  the aggregate AimLab report is not a substitute for a scenario-specific
  trigger assertion and oracle.
- If a relevant product constraint has no faithful fixture, label the result
  `EXPLORATORY / MISSING COVERAGE`; do not call it an optimization, release
  candidate, gameplay improvement, or regression proof.
- Distinguish measured, inferred, and assumed simulator inputs. A log-derived
  profile is not an exact replay, and a synthetic or counterfactual plant cannot
  establish live gameplay acceptance.
- Freeze fixtures, thresholds, seeds, identities, and covariates before reading
  candidate output. Convert a new live failure into a RED regression before
  changing production behavior.
- Use the fixed target schedule (default 1575 ms slot + 50 ms gap). A failure
  must wait out its slot, target opportunity counts must match, and a BodyLock
  isolate entry failure is an absolute hard gate with zero acquisition reward.
- Prefer the runtime profile's horizontal log-inferred controller response P50
  over an invented camera gain, but label it `inferred`: it is the controller's
  internal response belief, not independent game-plant calibration.
- Production acceptance still requires a matched native/live A/B and user feel
  confirmation after offline gates pass.

The accepted runtime keeps the complete controller chain in lockstep. The
retired `ai_proposal_mode`, `ai_proposal_hz`, and
`ai_proposal_update_stage` knobs must remain unknown/inert unless a new,
separately authorized RED-to-GREEN design proves same-tick authority/lifecycle
correctness and a whole-runtime benefit.
