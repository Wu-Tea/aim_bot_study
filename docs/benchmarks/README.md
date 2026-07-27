# Benchmark Index

Benchmark results are comparable only when revision, runtime/config
fingerprint, schema, seeds, duration and scenario semantics are compatible.
An older result without those identities is historical evidence, not an
authoritative baseline.

## Maintained contracts

- [Sustained AimLab](sustained-aimlab.md) — one-minute randomized ADS and
  BodyLock acquisition/tracking benchmark.
- [Vision blind window](vision-blind-window.md) — separates capture,
  publication, controller and delayed-response clocks.
- [Causal response shadow](causal-response-shadow.md) — shadow response
  estimation and evidence requirements.
- [Native runtime telemetry](native-runtime-telemetry.md) — runtime telemetry
  schema and causal journal.

## Historical result fixture

- [Axis stress A/B, seed 1337](axis-stress-ab-seed1337.md) — retained result
  record; do not treat a single seed as current acceptance.

## Archived result narratives

Older gamepad, ADS, manual-mix, mouse and native-controller result summaries
are indexed under
[Archived benchmark results](../archive/README.md#benchmark-results).

## Acceptance rule

A production policy change should:

1. reproduce a named defect with a deterministic fixture;
2. score the responsible controller contribution separately from physical
   input;
3. beat a fingerprinted current baseline on the defect metric;
4. preserve acquisition, tracking, smoothness and user-escape guardrails;
5. receive a live hand-feel smoke test before being called accepted.

Benchmarks exist to improve real runtime behavior. A score improvement that
changes scenario semantics or suppresses valid user input is not an
improvement.
