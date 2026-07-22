# Vision blind-window benchmark

This benchmark isolates the interval in which the controller is still acting on
an older vision result while newer reticle motion is already queued for later
delivery. Its first retained fixture (K1) is deliberately self-predictable: the
target does not reverse, accelerate, switch identity, or receive manual input.

The fixture baseline uses `stale_proportional_fixture_baseline`, a deliberately
simple stale controller that proves the fixture can expose pending-motion debt.
The production baseline reuses the same native adapter as the sustained AimLab
benchmark and sets `production_controller` to `true`. Production artifacts also
retain the config path and FNV-1a config fingerprint.

## Build and run

```powershell
cmake --build native/vision_native/build --config Release --target `
  cod_native_blind_window_benchmark_tests cod_native_blind_window_benchmark
ctest --test-dir native/vision_native/build -C Release `
  -R NativeBlindWindowBenchmarkTests --output-on-failure
& native/vision_native/build/Release/cod_native_blind_window_benchmark.exe `
  --output artifacts/benchmarks/blind-window/k1-fixture-baseline.json `
  --revision (git rev-parse HEAD)
& native/vision_native/build/Release/cod_native_blind_window_benchmark.exe `
  --policy production --assist-scale 1.0 `
  --config config.native.example.toml `
  --output artifacts/benchmarks/blind-window/k1-production-scale-1.00.json `
  --revision (git rev-parse HEAD)
```

The matrix contains 675 episodes: three retained seeds, three vision rates,
five capture phases, three fixed result latencies, and five response delays.
Raw episode metrics are retained alongside phase summaries and the ten worst
pending-debt episodes. Identical revision and CLI arguments must produce a
byte-identical JSON file.

The retained `0.70`, `0.80`, and `0.90` production mutations scale only the
benchmark-delivered assist. They test whether a proposed causal policy actually
improves timing or merely recreates a weaker global AI setting.

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
