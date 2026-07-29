# ADS timing decoupling experiment — 2026-07-29

## Question

Can ADS use a short arrival plan while retaining ownership long enough to
cover common sprint-to-fire latency, without reintroducing the old abrupt
ADS-to-BodyLock overshoot?

## Fixed inputs

- Seeds: `2026072901`, `2026072902`, `2026072903`
- Duration: 60 seconds per run
- Vision and target scripts are identical across candidates.
- ADS arrival horizon: 120 ms
- Intent fusion: vector
- Target motion: moving
- Evaluated separately:
  - normal POV (`off`)
  - full left-stick reversal plus random slide/jump POV motion (`combined`)
  - pure AI and mixed human+AI input

## Compared timing policies

- Legacy coupled baseline: arrival and forced handoff both at 120 ms.
- Decoupled: 120 ms arrival, 220 ms timed fallback.
- Hard delay: no ADS assist for the first 30 ms.
- Smooth ramp: ADS authority rises linearly during the first 30 ms.
- Ownership sweep: 160, 180, 200, and 220 ms.

Timed fallback in the final implementation is range-gated: at its deadline it
may hand off only when the residual error is inside the configured BodyLock
activation range (80 px in this fixture). A large residual remains in
`AdsAcquire`.

## Result

The hard 30 ms delay is rejected as a default. It moved median first assist
output from 1 ms to 31 ms, but reduced acquisition points in both normal and
combined-motion cases.

The 30 ms smooth ramp moved the first measurable assist output to 2 ms, but
its aggregate scores were effectively the same as the no-ramp decoupled
case. It remains an optional experiment and defaults to zero.

The useful change is the 120/220 decoupling with range-gated fallback:

| Scenario | Input | Acquire points | Tracking points | Missed targets | Handoff defect rate |
|---|---:|---:|---:|---:|---:|
| Normal legacy 120/120 | Pure | 6,470 | 12,140 | 79.0 | 7.6% |
| Normal 120/220 | Pure | 7,929 | 15,676 | 57.7 | 8.9% |
| Normal legacy 120/120 | Mixed | 7,971 | 19,265 | 59.3 | 13.6% |
| Normal 120/220 | Mixed | 9,169 | 20,509 | 45.0 | 10.3% |
| Combined legacy 120/120 | Pure | 7,864 | 7,210 | 84.0 | 60.0% |
| Combined 120/220 | Pure | 8,650 | 7,397 | 73.3 | 50.8% |
| Combined legacy 120/120 | Mixed | 8,814 | 9,813 | 75.3 | 52.9% |
| Combined 120/220 | Mixed | 9,904 | 10,539 | 63.7 | 50.0% |

Relative to the legacy baseline, 120/220 produced:

- normal pure: +22.5% acquisition, +29.1% tracking, -27.0% misses;
- normal mixed: +15.0% acquisition, +6.5% tracking, -24.1% misses;
- combined pure: +10.0% acquisition, +2.6% tracking, -12.7% misses;
- combined mixed: +12.4% acquisition, +7.4% tracking, -15.4% misses.

The residual risk is not hidden: pure-AI combined-motion overshoot per
acquired target increased materially. Mixed input, which represents actual
play, remained substantially better behaved, and its handoff defect rate
fell. This experiment therefore supports the decoupled timing contract, but
does not claim to solve the separate POV-motion prediction defect.

## Artifacts

- Final runnable `config.native.example.toml` replay:
  `artifacts/benchmarks/ads-timing-20260729-final-config/*.json`
- Original A/B/C/D runs: `artifacts/benchmarks/ads-timing-20260729/*.json`
- Range-gated 120/220 rerun: `artifacts/benchmarks/ads-timing-20260729-v3/*.json`
- Ownership sweep: `artifacts/benchmarks/ads-timing-20260729-sweep/*.json`
- 160 ms delay/ramp combinations:
  `artifacts/benchmarks/ads-timing-20260729-combo/*.json`

All JSON reports record the effective arrival horizon, fallback deadline,
delay, ramp, seed, and script hash.
