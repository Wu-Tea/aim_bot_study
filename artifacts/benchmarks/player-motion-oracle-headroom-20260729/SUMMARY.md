# Player-Motion Shadow Oracle Headroom — 2026-07-29

## Purpose

Measure how much the current production controller could improve if the
tracker could perfectly separate player/camera motion from target motion.
This is a benchmark-only hindsight/headroom tool. Production inputs,
configuration, Vision inference, and output behavior are unchanged when the
oracle is disabled.

All retained comparisons use:

- seeds `2026072901`, `2026072902`, and `2026072903`;
- 60-second runs;
- current `120/220 ms` ADS timing and vector fusion;
- moving targets;
- full left-stick reversal plus random jump/slide player motion;
- both pure and mixed manual input;
- both ADS acquisition and direct BodyLock cohorts.

The oracle-disabled run payload is bit-identical to
`player-jump-causal-model-final-20260729/combined-on.json`.

## Oracle variants

- `off`: current controller and causal jump cue.
- `state`: apply exact player-motion error displacement to tracker state, but
  do not export its raw rate.
- `full`: state correction plus the latest exact 1 ms player-motion rate.
- `forecast`: state correction plus exact average player-motion displacement
  over the next 32 ms. This is a planning headroom oracle, not a causal
  production algorithm.

## Combined-motion result

The table reports change from `off` to `forecast`.

| Cohort | Tracking | Mean error | Overshoot area | Max vertical overshoot | Stall ring |
|---|---:|---:|---:|---:|---:|
| ADS pure | **+10.2%** | **-5.5%** | **-7.2%** | -4.3% | -1.8% |
| ADS mixed | **+9.8%** | **-7.4%** | **-15.6%** | -2.4% | **-6.7%** |
| BodyLock pure | **+8.0%** | **-7.0%** | **-14.0%** | **-33.3%** | +3.1% |
| BodyLock mixed | **+7.2%** | **-5.3%** | **-8.2%** | -1.6% | -0.3% |

The full-rate oracle also establishes headroom without future knowledge:

- ADS tracking: `+8.9%` pure and `+6.7%` mixed;
- BodyLock tracking: `+3.9%` pure and `+3.2%` mixed;
- ADS mean error: `-6.3%` pure and `-5.4%` mixed;
- mixed ADS overshoot area: `-10.6%`.

The state-only oracle is not sufficient. It improves some acquisition and
mean-error values but loses tracking or increases overshoot in several
cohorts. Correct state decomposition must be paired with a usable short
motion forecast.

## Split scenarios

The 32 ms forecast has unequal value:

| Scenario | ADS tracking, pure / mixed | ADS overshoot, pure / mixed | BodyLock tracking, pure / mixed |
|---|---:|---:|---:|
| Left strafe only | +2.3% / -1.4% | +14.1% / +14.2% | +2.5% / +2.1% |
| Jump only | +8.3% / +4.3% | -18.5% / -27.1% | +5.7% / +5.0% |
| Slide only | +14.5% / +9.8% | -22.6% / -9.8% | +9.3% / +6.7% |

Interpretation:

- The existing left-stick response learner already captures much of smooth
  horizontal motion. Replacing it with an omniscient rate yields little
  tracking gain and worsens ADS overshoot.
- Vertical player motion is the large remaining headroom. Slide is the
  strongest defect, followed by jump.
- A single uniform ego-motion feed-forward policy is therefore not justified.

## Continued-push diagnostic

`continued_push_after_cross_ms` increases substantially under both exact-rate
and forecast oracles even while tracking, mean error, overshoot area, circle
exits, and magnitude-weighted wrong-way impulse improve.

For the combined forecast:

- ADS magnitude-weighted post-cross wrong-way impulse improves `10.3%` pure
  and `19.1%` mixed;
- ADS continued-push tick count increases `159%` pure and `111%` mixed;
- ADS center crossings fall `8.6%` pure and `6.2%` mixed;
- mixed ADS circle exits fall `6.2%`;
- p95 output delta is unchanged/improved (`-1.2%` pure, `-5.9%` mixed).

The count metric uses a fixed brake axis and a low `0.02` shaped-assist
threshold. In a moving-camera/moving-target episode it counts many small
bounded inertia ticks even when total wrong-way impulse and resulting
overshoot are lower. It remains a warning metric, but it cannot veto an
ego-motion candidate by itself. A future metric should measure avoidable
wrong-way impulse against the current relative target trajectory and report
both duration and magnitude.

## Decision

The headroom threshold is met: combined ADS tracking improves more than 5%
for both pure and mixed input, with lower mean error and lower overshoot.
Implementing a causal ego-motion observer is justified.

The production design must not expose one overloaded `error_rate`:

1. realized ego-motion displacement corrects tracker state;
2. target-only velocity remains a separate estimate;
3. short-horizon ego-motion forecast is exported separately with confidence;
4. ADS and BodyLock consume predicted displacement, not an unbounded
   one-frame derivative;
5. horizontal left-stick learning is retained unless a candidate beats it;
6. vertical jump/slide estimation is the first production target;
7. multi-target common motion may raise confidence without another Vision
   inference pass.

No production observer or controller policy is enabled by this benchmark
change.

## Verification

- Oracle-disabled combined run payload is bit-identical to the retained
  pre-Oracle combined baseline.
- `cod_native_target_coordinator_tests`: pass.
- `cod_native_sustained_aimlab_simulator_tests`: pass.
- Full Release build: pass.
- Registered CTest: 32 of 34 pass. The two failing gates are
  `NativeLeftStickMotionBenchmarkTests` (its pre-existing primary-quality
  threshold) and `NativeSustainedAimlabLearningTests` (warm rollout remains
  fallback-only). The files and policies behind those gates were not changed
  here, and the disabled-oracle payload equality rules out output drift in the
  retained combined fixture. Their parent-revision status was not rebuilt
  independently, so they remain explicit suite-health limitations rather
  than being labeled proven pre-existing failures.
