# DEC-2026-08-03-001: Protect the Live Baseline and Defer W5 Actuation

Status: accepted
Date: 2026-08-03
Confirmed by: user explicitly requested committing and backing up the current version, and explicitly corrected that W5 has not been completed
Related sessions: August 3 live bot-room acceptance, runtime protection and documentation sync
Related files:

- `docs/project/LIVE_ACCEPTED_RUNTIME_20260803.md`
- `docs/project/CURRENT_STATE.md`
- `native/vision_native/build/Release/cod_native_runtime.exe`
- `artifacts/runtime-backups/cod_native_runtime-live-accepted-20260803-872F6FFD.md`
- `native/control_learning/pending_motion_model.cpp`
- `native/vision_native/src/ego_motion_observer.cpp`

Supersedes: none
Superseded by: none

## Context

The current runtime produced a large positive live change after several control,
identity, ADS-lifecycle and SDL input fixes. The user asked to protect it before
more work. Both accepted games used the same LMG with an approximately 400 ms
weapon ADS time, so this trial demonstrates practical stability and usability
but does not isolate parameter fit for faster weapons.

The source also contains background ego-motion observation, PendingMotion timing
repairs and rollout telemetry. Those facilities can look like memory-related
work, but they are diagnostic shadow paths. The planned W5 architecture requires
causal accounting of scheduled, in-flight and realized camera work over a short
horizon and does not yet exist in production actuation.

## Decision

- Treat runtime SHA-256 `872F6FFFD1598C64ABC34E0D551F4C112B40FEAC630844C6CB0B0FEE50558C38`, its matching config snapshot and source commit `5d2f3be` as the August 3 rollback baseline.
- Restore the executable and config together; executable hash is the primary runtime identity when a session's build-time Git field predates the protection commit.
- Do not claim that W5, CausalMotionLedger, Causal Remaining v2, or 150-200 ms short-term memory affects current output.
- Keep W3 ego-motion and PendingMotion/rollout data shadow-only until their live timing, quality and causal response relationship are measured.
- Do not tune ADS from the 400 ms LMG sample. First run a comparable approximately 260 ms weapon trial with the accepted runtime/config unchanged.

## Reasons

- A known-good rollback point prevents future experimental work from destroying the first strongly positive live baseline.
- Separating implemented shadow observability from future memory actuation prevents false attribution and unsafe control changes.
- A faster-ADS comparison reduces weapon-timing confounding before controller parameters are changed.
- Keeping the final-output fuser as the sole arbitration owner preserves the architecture that produced the live improvement.

## Rejected Alternatives

### Treat Current Learning/Shadow Fields as Proof That W5 Is Active

Rejected because no CausalMotionLedger/Causal Remaining v2 feeds actuation, and
newly submitted commands are not yet reconciled as realized camera displacement.

### Tune ADS Immediately From the 400 ms LMG Trial

Rejected because one slow weapon cannot distinguish weapon animation/readiness
timing from acquisition-controller timing.

### Protect Only the Executable

Rejected because behavior depends on the effective config; a binary-only backup
is not a reproducible rollback point.

### Resume Additive or Fixed-Preservation Manual/AI Mixing

Rejected because the accepted behavior comes from solving one bounded final
target-relative output, not preserving two independently accumulated forces.

## Evidence

- User report: two games at roughly 2.5 KD and 4 KD with behavior substantially better than before.
- User report: both games used one LMG with approximately 400 ms weapon ADS time; an approximately 260 ms test is planned.
- Repository verification: Release CTest passed `36/36` before the protection commit.
- Runtime verification: installed and backup executable hashes match exactly; the config snapshot has SHA-256 `E205BC7497710006F688F0347B9ABB9E24AAE5A20A5D99DF80098175B6DEEA26`.
- Source commit message explicitly states PendingMotion/rollout remains shadow-only and does not implement W5 or affect actuation.

## Consequences

- Future candidates must identify their source, executable hash, config hash and rollback path against this baseline.
- The next live test changes the weapon timing cohort, not controller code or config.
- W4/W5 work must begin with shadow validation and cannot become another output owner.
- Positive results from the current runtime must be attributed to verified control/input changes, not unspecified memory or learning.

## Review Triggers

Review this decision if the approximately 260 ms trial reproduces strong pull-away,
undertravel, lazy acquisition, stick loss, or identity jumps; if W3 live cost or
quality fails its gate; or when W5 has a separately reviewed shadow implementation
with scheduled/in-flight/realized reconciliation evidence.
