# ADS / BodyLock jump stability — Codex final verification

Date: 2026-08-01 (Asia/Hong_Kong)

> This report is the final pre-Task-4/pre-deployment checkpoint. Conditional
> Task 4, runtime installation and user live testing were completed afterward;
> see the [current live acceptance](../../../../docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md).

## Result

- Task 1 synthetic acceptance: **PASS**. A near-full physical stick request now takes exact manual ownership before `Reacquiring` release smoothing, and a shaped AI vector above unit magnitude cannot block it.
- Task 2 synthetic acceptance: **PASS**. The existing target/mode-aware ADS -> BodyLock handoff remains inside its contract; the matched left-stick report keeps maximum transition AI delta at `0.064` and maximum transition overshoot at `1.86941 px`.
- Task 3 build/test and matched benchmark acceptance: **PASS**. All registered and focused tests pass, and four matched vector-fixture matrices have zero run-field differences.
- Conditional identity-chain repair: **PASS**. When the selector identity protocol explicitly publishes no selected target, the coordinator no longer acquires an arbitrary detector candidate. The left-stick production-chain defect count falls from `1` to `0`.
- Runtime deployment: **NOT PERFORMED**. Live gameplay/video feel remains a separate user acceptance step.

## Provenance

| Item | Identity |
|---|---|
| Repository HEAD | `5d9f6d37703d5b0d2af009ae5389ee5d95671710` |
| Config | `config.toml`, SHA-256 `6CA2D349D13F45645BF93ADB7D7793BDC360D649A5553203DDD62BAD9A63E1CB`, benchmark FNV-1a64 `6672529141336852435` |
| Live runtime (untouched) | SHA-256 `3D9ED74CBCD7007F9EEF3CACC6B17EB5763F7056047BE35AC37495BF1CEB7402` |
| Luna vector baseline benchmark | `cod_native_sustained_aimlab_benchmark-luna-F3E356CB.exe`, SHA-256 `F3E356CB10900D9A7E8055DB09892F40E2C0743E1E568EEA3FEFEACDC2594DF6` |
| Final vector candidate benchmark | `cod_native_sustained_aimlab_benchmark-candidate-15C8A963.exe`, SHA-256 `15C8A963DB1C8FD5892A014BBA9FDAC98F63BFE444195795B7A7A8DAAB95DA9F` |
| Luna left-stick baseline | `cod_native_left_stick_motion_benchmark-luna-873B6433.exe`, SHA-256 `873B6433D4EC4E0A7479590E957FA8D4A50A57DE3A2D24E4385C51EFCE802F7B` |
| Final left-stick candidate | `cod_native_left_stick_motion_benchmark-candidate-73C45F56.exe`, SHA-256 `73C45F56C1249DF501BBF61B0F25234790F225422E0B026EB23BCB46B035F921` |
| Luna fuser/shaper source-diff fingerprint | Git object `8b88e27212923c12d7defe181aebf72b986d85b0` |
| Final relevant source-diff fingerprint | Git object `a23d0f54c47541441e101631e1b54e3da5d17d02` |

The executable hashes are the authoritative A/B identity. The repository is intentionally dirty, so the recorded commit alone is not treated as sufficient provenance.

## RED -> GREEN evidence

1. `VectorIntentFuser`
   - RED: `full manual escape must preempt reacquiring release slew`.
   - GREEN: exact `-1.0` manual output during `Reacquiring`; `manual_escape=true`, candidate `ManualOnly`.
   - GREEN: exact `-1.0` manual output against a `+1.34` shaped AI proposal.

2. Selector-owned target admission
   - RED: `selector-owned candidate without a selection must stay manual`.
   - GREEN: `selector_identity_protocol=true` plus `preferred_source_id=0` returns no candidate; an explicit non-zero selector choice remains eligible.

3. Retained learning contract
   - Observed warm-round response confidence: `0.980572`.
   - Observed delay confidence: `0`, below the rollout gate `0.05`.
   - Correct safe behavior: `valid_rollout_decisions=0`, `changed_scale_decisions=0`, `mean_selected_scale=1`.

## Test results

- Rebuilt all 32 executable targets referenced by the Release CTest manifest, without building `cod_native_runtime`.
- Full registered suite: **34/34 passed**.
- Focused direct executables all passed:
  - `cod_native_vector_intent_fuser_tests.exe`
  - `cod_native_aim_dynamics_shaper_tests.exe`
  - `cod_native_controller_tests.exe`
  - `cod_native_target_coordinator_tests.exe`
  - `cod_native_bodylock_follow_controller_tests.exe`
- `git diff --check`: exit `0` (line-ending warnings only).

## Matched vector A/B

Every pair used the frozen Luna vector executable and the final candidate executable with the same current config, seeds, scenario arguments, profiles and cohorts.

| Fixture | Runs | Coverage | Baseline/candidate run-field differences |
|---|---:|---|---:|
| Sustained ordinary moving + 36 ms occlusion + full reversal | 12 | 3 seeds, pure/mixed, ADS/BodyLock | `0` |
| Fuser continuity | 6 | 3 seeds, mixed, ADS/BodyLock, compound directional, recoil/body-box disturbance | `0` |
| Micro-input dropout/decoy | 6 | 3 seeds, micro input, ADS/BodyLock, 160 Hz | `0` |
| Horizontal aim bias | 3 | 3 seeds, mixed BodyLock | `0` |

Selected sustained aggregates are therefore exactly unchanged:

| Metric | Luna vector baseline | Final candidate | Delta |
|---|---:|---:|---:|
| Tracking points | `271902.65303` | `271902.65303` | `0` |
| Direction discontinuities | `59` | `59` | `0` |
| False interruptions | `9` | `9` | `0` |
| False stops | `0` | `0` | `0` |
| Overshoot area | `1057579.17688` | `1057579.17688` | `0` |
| Continued push after crossing (ms) | `245` | `245` | `0` |
| Post-occlusion error area | `1258734.02854` | `1258734.02854` | `0` |
| Maximum run P95 output delta | `0.1999999997` | `0.1999999997` | `0` |

This establishes non-regression for the existing vector controller. The two newly added escape cases are exercised by focused unit tests rather than by these pre-existing scenario scripts.

## Production-chain reacquisition A/B

| Metric | Luna baseline | Final candidate |
|---|---:|---:|
| Defect count | `1` | `0` |
| Production-chain gate | `false` | `true` |
| Reacquired error (px) | `215` (distractor) | `51` (selected target) |
| Useful-assist latency (ms) | `-1` (never useful in window) | `0` |
| Reacquire maximum output delta | `0` | `0.064` |
| Selected-track changes | `3` | `1` |
| Defect reason | `reacquire_not_bumpless` | none |

The candidate restores useful assist immediately while staying below the existing `0.07` output envelope.

## Artifact hashes

| Artifact | SHA-256 |
|---|---|
| `sustained-luna-vector-baseline.json` | `9A10A07EBDF0DEB185E51AF1DA8DD06EC9D182FED8F4B85F3BD551AA48D4EBE8` |
| `sustained-codex-final-candidate.json` | `58A0238AF4A4E13CB513E5A9221C766FDCD14FF35951BD8031ACAA64A9FBF38B` |
| `fuser-luna-vector-baseline.json` | `EF4C43A7D5D2895533E3BC22E64828A5C2E48AA9ACE5A3D050652CFEF2847532` |
| `fuser-codex-final-candidate.json` | `3D18C966DBF60427CB3C7DBFF2D641390CFD55849DD0D63C15293F9E51EAADE1` |
| `micro-luna-vector-baseline.json` | `7A202558CAADC578961A238D754BBEAAAC96D40CFEBB0CD6E9B61B8D0FF758CB` |
| `micro-codex-final-candidate.json` | `69E4229CBF816969D3EC8D820DDDABF4C75D9D860F708C48BAA0B00E213A78A4` |
| `bias-luna-vector-baseline.json` | `FBBAC0C19DF56280646A2D49B17C92B694CD4CC3FA45276EA4B3640C7C4B0678` |
| `bias-codex-final-candidate.json` | `CFB546D045A4E35C5C58B75BA1C954AE0847B6F06893F99A42D0BF0707CD5066` |
| `left-stick-luna-baseline.json` | `03FF2FD80DD9C8DFF2E72C59A6C343ADECC13CBCDB84AE7531603904AB560E2B` |
| `left-stick-codex-final-candidate.json` | `D24CB7ADDA23A057F8395AFB5AC02676772B1009E534ACCF36ED46CF6D6632FF` |

## Remaining acceptance boundary

No candidate runtime was built into or copied over the live runtime path, and no process was restarted. The remaining step is an explicitly authorized candidate-runtime build/deployment followed by the same four-video last-character live-feel check. This report does not claim live acceptance.
