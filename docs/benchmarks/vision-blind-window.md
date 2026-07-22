# Vision blind-window benchmark

This benchmark isolates the interval in which the controller is still acting on
an older vision result while newer reticle motion is already queued for later
delivery. Its first retained fixture (K1) is deliberately self-predictable: the
target does not reverse, accelerate, switch identity, or receive manual input.

The B2 checkpoint uses `stale_proportional_fixture_baseline`, a deliberately
simple stale controller that proves the fixture can expose pending-motion debt.
It is **not** a score for `NativeGamepadController`; JSON reports therefore set
`production_controller` to `false`. A production result must not be claimed
until the reusable native controller adapter is connected.

## Build and run

```powershell
cmake --build native/vision_native/build --config Release --target `
  cod_native_blind_window_benchmark_tests cod_native_blind_window_benchmark
ctest --test-dir native/vision_native/build -C Release `
  -R NativeBlindWindowBenchmarkTests --output-on-failure
& native/vision_native/build/Release/cod_native_blind_window_benchmark.exe `
  --output artifacts/benchmarks/blind-window/k1-fixture-baseline.json `
  --revision (git rev-parse HEAD)
```

The matrix contains 675 episodes: three retained seeds, three vision rates,
five capture phases, three fixed result latencies, and five response delays.
Raw episode metrics are retained alongside phase summaries and the ten worst
pending-debt episodes. Identical revision and CLI arguments must produce a
byte-identical JSON file.

## Interpretation

`harmful_pending_at_reveal_px` measures already scheduled reticle motion that
opposes the revealed error or continues farther than the remaining error.
`future_burden_80_px_ms` integrates error over the first 80 ms after reveal.
`reverse_correction_80_stick_ms` measures how much opposite input is needed to
undo stale momentum. `reveal_to_reacquire_ms = -1` means the target did not stay
within the 8 px reacquire radius for 30 ms before the fixture ended.

Do not collapse these metrics into a total score. Improvements must reduce the
K1 debt metrics while preserving the ADS acquisition, far-error closing-speed,
useful-crossing, output-smoothness, interruption, identity, and authority
guardrails defined by the benchmark specification.
