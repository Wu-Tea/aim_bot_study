# DEC-2026-08-21-001: Layer Native Tests into Base and Functional Runners

Status: proposed
Date: 2026-08-21
Confirmed by: user requested that the proposal be recorded; implementation is not yet confirmed
Related sessions:
- 2026-08-21T03:21:55+08:00
Related files:
- native/vision_native/CMakeLists.txt
- native/runtime_app/runtime_timing_tests.cpp
- tests/test_native_controller_behavior.py
- tests/test_native_cpp_runtime_scaffold.py
- tests/test_native_vision_scaffold.py
- tools/check_native_cpp_gamepad_runtime.ps1
Supersedes: none
Superseded by: none

## Context

The repository test audit found 73 registered native CTest entries backed by
71 native test source files, at least 392 named test/scenario functions, and
19 registered gameplay incident regressions. The native suite was fast and
GREEN (`73/73`, about 0.5 seconds in parallel), so execution time was not the
primary defect. The maintenance defect was that production contracts, optional
features, offline benchmarks, migration scaffolds, Python fallback behavior and
incident fixtures shared one flat test namespace.

The Python side contained 89 test modules and about 910 test functions without
pytest lifecycle markers. Three native-migration scaffold modules contained 32
tests; a focused run produced 28 passes and 4 failures because the tests still
expected retired file names, fields or constants while the current native CTest
suite remained GREEN.

The user rejected mechanically choosing 30 tests by name and asked for all test
responsibilities to be scanned before replanning. The user then requested that
the more compressed Base-versus-functional approach be recorded. A separate
session is investigating the live BodyLock crosshair-sticking symptom; no result
from that investigation is assumed here.

## Decision

Propose replacing the flat native test target list with two physical runners
and approximately ten logical CTest suites. Preserve named internal cases,
incident IDs, trigger coverage, counterfactuals and JSON oracle output even when
their current standalone `main()` functions are removed.

The proposed Base runner always protects the current production gameplay path:

1. `BaseContracts`: runtime configuration, `640x512 -> 480x384` resize contract,
   pipeline types, committed capture and Vision-to-Controller adaptation.
2. `BaseVisionSelection`: target selection, cue/friendly filtering, identity
   continuity and target-size-scaled pickup, including acceptable edge-small,
   rejected far-large and permitted genuinely near-large targets.
3. `BaseRuntimeFreshness`: VisionService freshness/no-replay, keepwarm authority,
   input recovery, scheduling and neutral shutdown.
4. `BaseAds`: scope readiness, ADS admission/lifecycle, one-press token, close
   pacing, center crossing, existing/far-selected targets and evidence gaps.
5. `BaseBodyLock`: coordination, geometry, cue continuation, target motion,
   manual/AI arbitration, reversal, high-frequency and sustained-motion cases.
6. `BaseEndToEnd`: production controller integration, physical passthrough,
   single final-output ownership, finite bounds and independent fire/recoil
   composition.

The proposed Functional runner protects supported but optional subsystems:

1. `FeatureDynamicViewport`: capture viewport growth, shrink and edge rescue.
2. `FeatureRecoilAndWeapon`: recoil profiles, calibration, fallback and weapon
   identity/recognition.
3. `FeatureAutoFireAndMarker`: freshness/cadence/manual takeover, person marker,
   D-pad output and cooldown behavior.
4. `FeatureTelemetryAndDiagnostics`: queue/rotation/session/provenance, target
   identity, event sampling, ADS visual transitions and collectors.

AimLab, PID baselines, scenario generators, scorers, trace tools and left-strafe
smokes should be explicit offline benchmark targets rather than default CTest
entries. Python tests should be routed separately as base, functional,
fallback and benchmark lanes; supported Python fallback behavior must not be
deleted merely because the native runtime is the production default.

The target-size-scaled ADS pickup envelope and scope-readiness authority are
Base behavior. The optional dynamic capture viewport remains Functional even
when disabled in the active configuration.

## Reasons

- Test ownership should follow production owners and product capabilities, not
  historical implementation phases or one executable per source file.
- Two runners remove repeated harnesses and dozens of build targets while a
  suite/case registry can retain exact failure names and focused reproduction.
- Gameplay incident regressions remain hard evidence even when represented as
  named cases inside Base suites instead of standalone executables.
- Optional telemetry, recoil, marker and dynamic-viewport behavior should remain
  testable without making every runtime-only build compile every feature test.
- Offline benchmark validity is different from a deterministic product gate and
  should not be communicated as unit-test coverage.

## Rejected Alternatives

- Keep an arbitrary list of 30 existing CTest names: rejected because the count
  mixes multi-case executables, incident fixtures and repeated CLI invocations.
- Wrap all existing executables in one driver without refactoring ownership:
  rejected because it would only hide the entry count and preserve duplicated
  `main()` functions and harness code.
- Delete all Python gamepad or vision tests: rejected while Python fallback and
  Python mouse/tooling paths remain supported.
- Move current ADS and BodyLock incident regressions to an occasional extended
  suite: rejected because they define accepted production gameplay behavior.

## Evidence

- Native CTest audit: 73 registered entries, 19 incident regressions, all GREEN
  in the current Release build.
- Repository inventory: 71 native test/incident files and 89 Python test modules.
- Focused stale-scaffold run: 4 failures among 32 tests due to obsolete source
  shape assumptions rather than a native production regression.
- `RuntimeTimingTests` also contains an AimScope/manual-fire incident harness,
  demonstrating responsibility leakage between suites.
- The legacy AimLab CLI prints PASS without a gameplay quality threshold; the
  sustained smoke validates execution shape rather than natural aim quality.
- User direction: scan responsibilities first, then record a more compressed
  Base-versus-functional design rather than preserving an arbitrary quota.

## Consequences

- Implementation requires a small internal case registry, two runner entry
  points and migration of existing test functions away from standalone mains.
- CTest-level reporting becomes suite-level, so runners must print every failed
  case, support `--suite` and `--case`, continue collecting independent failures,
  and preserve per-incident JSON reports.
- Migration must temporarily run old and new forms together and compare case and
  oracle coverage before deleting old targets or files.
- The first confirmed cleanup candidate is the 32-test Python migration-scaffold
  group, after any still-useful launcher/build behavior is migrated to real
  acceptance tests.
- No production controller, BodyLock, ADS, Vision, recoil or configuration code
  is changed by this proposal itself.

## Review Triggers

- The parallel BodyLock investigation produces a new accepted incident or
  changes an existing product contract.
- Suite-level CTest reporting proves insufficient for retry, sharding or failure
  isolation even with named runner cases.
- An optional feature becomes part of the always-on production contract.
- Python gamepad/vision fallback is formally retired or promoted again.
- Coverage comparison finds an existing incident trigger, counterfactual or
  oracle that cannot be preserved inside the proposed runners.
