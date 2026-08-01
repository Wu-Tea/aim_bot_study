# Task 4 target admission / ownership verification

Date: 2026-08-01 (Asia/Hong_Kong)

## Result

Task 4 passes synthetic and matched acceptance. The retained production change
is deliberately small: `VectorIntentFuser` remembers the last non-zero target
identity for the current physical LT epoch. If ownership expires and a different
target appears while LT is still held, the replacement spends one controller
tick at exact manual / zero AI and then re-enters through the existing `0.08`
vector slew envelope. A deliberate new LT epoch still calls `reset()` and keeps
initial ADS acquisition immediate.

Fresh Vision position remains authoritative. No tracker innovation clamp,
second position state, selector rewrite, ADS re-arm, gain reduction, or new
output brake was added.

Live status: **PASS**. After the authorized in-place runtime replacement, the
user completed a gameplay test and accepted the control feel. The durable live
record is
`docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md`.

## RED -> GREEN

- Same-track geometry fixture: one fresh observation moves about `70 px` while
  preserving the canonical target and BodyLock mode. The fresh plan consumes
  the movement, while final vector output delta remains `<= 0.0805`.
- Replacement fixture: after a bounded no-target gap, the selected replacement
  is `95 px` from the last observed target while LT remains held.
  - RED: first replacement output was `0.064`, with no fusion fallback.
  - GREEN: first replacement output is exact manual / zero AI through the
    explicit `TargetChanged` admission path; the next tick is positive and
    `<= 0.0805`.
- Counterexample guardrail: a close, legitimate same-track lateral runner keeps
  fresh plan position, canonical ownership, BodyLock mode, and material follow
  authority without entering the target-admission fallback.

## Tests

- Rebuilt all 32 Release executable targets referenced by the CTest manifest,
  without building `cod_native_runtime`.
- Registered suite: `34/34` passed.
- Focused direct executables passed:
  - `cod_native_vector_intent_fuser_tests.exe`
  - `cod_native_aim_dynamics_shaper_tests.exe`
  - `cod_native_controller_tests.exe`
  - `cod_native_target_coordinator_tests.exe`
  - `cod_native_bodylock_follow_controller_tests.exe`
- `git diff --check`: exit `0` (line-ending warnings only).

## Matched sustained A/B

Both executables used `config.toml`, seeds `1337/7331/2026`, pure/mixed input,
ADS/BodyLock, moving ordinary targets, `36 ms` occlusion, full-reversal strafe,
causal player-motion mode, vector fusion, and current-error remaining work.

The pre-Task-4 candidate was run-field identical to the frozen Luna baseline in
this matrix, so the following difference is attributable to Task 4.

| Metric | Frozen baseline | Task 4 candidate | Delta |
|---|---:|---:|---:|
| Acquire points | `406249.47197` | `406494.28256` | `+0.0603%` |
| Tracking points | `271902.65303` | `271995.91549` | `+0.0343%` |
| Overshoot area | `1057579.17688` | `1056825.77419` | `-0.0712%` |
| Continued push | `245 ms` | `245 ms` | `0` |
| Direction discontinuities | `59` | `59` | `0` |
| False interruptions | `9` | `9` | `0` |
| False stops | `0` | `0` | `0` |
| BodyLock entry failures | `0` | `0` | `0` |
| Maximum run P95 output delta | `0.1999999997` | `0.1999999998` | effectively `0` |

Worst per-run tracking change was `-0.4877%`, inside the `-2%` guardrail.
Expected target-admission fallback ticks increased by `351`; aggregate
unexpected-mode time increased by `114 ms` (`+4.185%`), inside the `5%`
guardrail.

## Production-chain identity benchmark

Compared with the accepted pre-Task-4 candidate:

| Metric | Before Task 4 | Task 4 candidate |
|---|---:|---:|
| Defect count | `0` | `0` |
| Desired gate | `true` | `true` |
| Selected-track changes | `1` | `1` |
| Reacquired error | `51 px` | `51 px` |
| Useful-assist latency | `0 ms` | `1 ms` |
| Maximum reacquire output delta | `0.064` | `0.064` |
| Scenario-summary differences | `0` | `0` |

The intentional one-tick admission delay is visible without reintroducing the
blind-candidate defect or increasing the output-delta bound.

## Provenance

| Item | Identity |
|---|---|
| Repository HEAD | `5d9f6d37703d5b0d2af009ae5389ee5d95671710` |
| Config file | SHA-256 `6CA2D349D13F45645BF93ADB7D7793BDC360D649A5553203DDD62BAD9A63E1CB` |
| Effective config provenance | SHA-256 `16FA226298A7957D98AC3635F417C1C74CC2AC6BE2CE7EEAFDF305CD15D48033` |
| TensorRT engine | SHA-256 `45FC56274FF3BBC659E534C3B7833065B0483EF8022AC5D7657CD6DA7DBDEB21` |
| Relevant dirty source diff | SHA-256 `DB7D86F17AEB6B4D34FBC92B775473FA65E4FAC164D704FE97D179328DC55B5E` |
| Frozen Luna sustained baseline | SHA-256 `F3E356CB10900D9A7E8055DB09892F40E2C0743E1E568EEA3FEFEACDC2594DF6` |
| Task 4 sustained candidate | SHA-256 `BE153AC96A04513CEDF7D3C2B0573758A7532560F6DB6417B0F0E5BAB2A36CE6` |
| Task 4 left-stick candidate | SHA-256 `4279D53F0EF603960046CE8F413D866300C46F73E0EE735EF5A4292415EA8DD8` |
| Sustained baseline JSON | SHA-256 `BF2CE2E17FD7B0C75B9905632349D1959479383DC9FFFD04293FA0EE8F4131CF` |
| Sustained candidate JSON | SHA-256 `3F2E2B7CAFA526D4AFB4A7044B7602E6AB8BA6CB28FF489124EDF00080FD850D` |
| Left-stick candidate JSON | SHA-256 `EB873EE16E17B580189C44E38DDD7295B811470DC2409D98F7323787775E1D17` |
| Task 4 runtime installed at the current runtime path | SHA-256 `B7F9F28B6A39E1AE58DB75C5F3A3A18DDF3D9692886245ABF2C5493FDC84140C` |
| Archived Task 4 runtime | `artifacts/runtime-candidates/20260801-task4-ownership-admission/cod_native_runtime-task4-B7F9F28B.exe` |
| Previous runtime backup | SHA-256 `3D9ED74CBCD7007F9EEF3CACC6B17EB5763F7056047BE35AC37495BF1CEB7402` |

## Runtime installation and remaining boundary

After explicit user authorization, the Release `cod_native_runtime` target was
built in place and replaced the executable at the current launcher path. The
old executable was preserved at
`artifacts/runtime-backups/20260801-task4-before-overwrite/cod_native_runtime-pre-task4-3D9ED74C.exe`.

`--dump-effective-config` completed with exit code `0`, confirming the expected
`640x512 -> 480x384 @ 160 FPS` contract, current config, TensorRT engine and
build commit. At build-verification time the runtime process was not started or
restarted; the user subsequently launched the installed runtime and completed
the live acceptance step.

## Subsequent live acceptance

User-confirmed behavior:

- severe ADS sudden pulls no longer reproduce;
- after a few early imperfect acquisitions, later acquisitions were nearly
  direct to the person;
- BodyLock retained a stationary target while the main view moved;
- moving follow no longer showed the prior frequent jitter.

The improving-with-session observation is not attributed to the top-level
`rollout_shadow` learner: that path records shadow telemetry and does not alter
production output. The in-memory `AimResponseEstimator` does feed response
scale/confidence into the live coordinator, so it is a plausible contributor
along with tracker/body-geometry state establishment. This causal attribution
is AI-inferred and was not promoted to a user-confirmed fact.
