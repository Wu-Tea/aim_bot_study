# August 3 Live-Accepted Native Runtime

**Status:** protected live baseline
**Source commit:** `5d2f3be` (`fix(controller): protect live-accepted aiming baseline`)
**Scope:** native C++ gamepad runtime used for the August 3 bot-room trial

## What This Baseline Is

This is the rollback point for the first runtime that the user reported as
substantially better across complete matches. It is used by the normal native
launcher and combines the current target selector/tracker, ADS and BodyLock
controllers, final manual/AI output arbitration, recoil feed-forward and ViGEm
delivery.

The protection has two layers: source/config behavior is preserved by Git, and
the exact installed executable plus effective config is preserved as a local
runtime backup.

## Identity and Restore Set

- Installed runtime: `native/vision_native/build/Release/cod_native_runtime.exe`
- Runtime SHA-256: `872F6FFFD1598C64ABC34E0D551F4C112B40FEAC630844C6CB0B0FEE50558C38`
- Backup executable: `artifacts/runtime-backups/cod_native_runtime-live-accepted-20260803-872F6FFD.exe`
- Backup config: `artifacts/runtime-backups/cod_native_runtime-live-accepted-20260803-872F6FFD.config.toml`
- Config SHA-256: `E205BC7497710006F688F0347B9ABB9E24AAE5A20A5D99DF80098175B6DEEA26`
- Backup manifest: `artifacts/runtime-backups/cod_native_runtime-live-accepted-20260803-872F6FFD.md`
- Verification: Release CTest `36/36` passed; installed and backup executable hashes match exactly.

Restore the executable and config snapshot together. A session manifest may show
the pre-commit build HEAD because the binary was built before the exact working
tree was protected; the executable SHA-256 plus this commit/backup mapping is the
authoritative identity.

## Live Evidence and Its Limit

- **User-confirmed:** two games were roughly 2.5 KD and 4 KD, and the application felt substantially better than the preceding runtime.
- The accepted runtime no longer exhibited the previous pervasive manual-plus-AI force stacking or missing-stick regression during those games.
- Both games used one LMG with an approximately 400 ms weapon ADS time.
- Therefore this run is strong evidence for gross control stability, input continuity and practical usability, but it is not a clean cross-weapon ADS timing calibration.
- The next comparison should use an approximately 260 ms weapon while keeping runtime, config and sensitivity unchanged.

## Control Behavior in This Version

- Manual and AI enter one `VectorIntentFuser` as proposals for a single final target-relative stick vector. Wrong direction or excessive predicted travel may be limited for either source; the system does not simply add two forces.
- Fresh ADS/BodyLock position, stopping demand and the active mode's force envelope bound the final output after slew.
- BodyLock position correction is separate from bounded target-motion feed-forward, so retained motion cannot freely reverse a meaningful fresh positional correction.
- Confirmed selector replacement is an identity boundary that resets target geometry/control state without rearming a held physical ADS epoch.
- ADS spatial admission is `135 px`. Nominal acquisition is `135 ms` from the first eligible target admission; continuation is conditional and never exceeds the `220 ms` hard ceiling.
- Vision/control provenance uses frame/observation/target/acquisition/tick join keys and records explicit rejection reasons and effective activation radius.
- SDL event pumping is centralized so both sticks remain live together with the other controller inputs.
- OCR/profile selection is not in the runtime hot path. Recoil profile playback is disabled; fixed downward feedback remains the default recoil behavior.

## W0-W6 Status Boundary

The work-package labels describe implementation stages, not enabled product
features:

| Stage | Current status | Production effect |
| --- | --- | --- |
| W0-W2 | Acquisition/provenance foundation implemented and tested | ADS admission/lifecycle, joinable stage timing, rejection reasons and latest-frame control contracts are active where applicable |
| W3 | Latest-only background ego-motion observer integrated as shadow telemetry | No control authority; live GPU cost, valid rate and quality still need measurement |
| W4 | Real ViGEm-to-background response/delay identification is not complete | The older response estimator does not count as W4 completion |
| W5 | CausalMotionLedger/Causal Remaining v2 is not implemented | No 150-200 ms short-term memory affects output; PendingMotion and rollout remain shadow-only |
| W6 | Activation/tuning gate not entered | Blocked on successful W4/W5 shadow evidence and a separate review |

In particular, the good live result must not be attributed to W5 memory. It is
consistent with the final-output envelope, fresh-position boundary, target
identity/ADS lifecycle fixes and restored input polling.

## Next Live Check

Use the same runtime/config and an approximately 260 ms ADS weapon. Preserve the
whole telemetry session and record only a short note for any obvious event. The
comparison should answer:

1. Does initial ADS remain direct without strong overshoot or stopping short?
2. Does the 135 ms nominal window extend only for a still-visible, unacquired target?
3. Does a center crossing reverse promptly without old-direction BodyLock carry?
4. Does target replacement reset geometry without a new held-LT snap?
5. Do both sticks remain continuously available for the full session?

Do not change controller parameters before this comparison; otherwise weapon
timing and algorithm changes cannot be separated.
