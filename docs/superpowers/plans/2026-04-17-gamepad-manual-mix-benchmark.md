# Gamepad Manual-Mix Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an independent manual-mix benchmark suite that reuses target-motion manifests, injects deterministic simulated player stick input, and reports both tracking and manual/AI mixing metrics.

**Architecture:** Keep the current `phase1` benchmark untouched as the clean closed-loop baseline. Add a parallel manual-mix path with its own input generator, metric evaluator, and scoreboard output, while reusing existing target manifests and scoreboard helpers where that keeps behavior consistent.

**Tech Stack:** Python 3, `unittest`, dataclasses, existing benchmark runner/scoreboard pipeline

---

### Task 1: Build the deterministic manual input generator

**Files:**
- Create: `D:\work\AI\yolo-study-001\tests\gamepad\manual_mix_inputs.py`
- Create: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_manual_mix_inputs.py`

- [ ] **Step 1: Write the failing generator tests**

```python
import unittest

from tests.gamepad.manual_mix_inputs import (
    ManualInputFrame,
    ManualMixInputConfig,
    generate_manual_mix_frames,
)
from tests.gamepad.benchmark_scenarios import generate_phase1_manifests


class ManualMixInputTests(unittest.TestCase):
    def test_same_manifest_and_seed_generate_identical_manual_frames(self):
        manifest = generate_phase1_manifests("mix-suite", 12345)[0]
        config = ManualMixInputConfig()

        first = generate_manual_mix_frames(manifest, manual_seed=7, config=config, sim_frames=60)
        second = generate_manual_mix_frames(manifest, manual_seed=7, config=config, sim_frames=60)

        self.assertEqual(first, second)

    def test_different_manual_seeds_change_the_generated_sequence(self):
        manifest = generate_phase1_manifests("mix-suite", 12345)[0]
        config = ManualMixInputConfig()

        first = generate_manual_mix_frames(manifest, manual_seed=7, config=config, sim_frames=60)
        second = generate_manual_mix_frames(manifest, manual_seed=8, config=config, sim_frames=60)

        self.assertNotEqual(first, second)

    def test_generator_emits_at_least_one_opposing_burst_annotation(self):
        manifest = generate_phase1_manifests("mix-suite", 12345)[8]
        config = ManualMixInputConfig()

        frames = generate_manual_mix_frames(manifest, manual_seed=3, config=config, sim_frames=120)

        self.assertTrue(any(frame.in_opposing_burst for frame in frames))
        self.assertTrue(any(frame.mode == "opposing_burst" for frame in frames))
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.gamepad.test_gamepad_manual_mix_inputs -v`
Expected: FAIL because `tests.gamepad.manual_mix_inputs` does not exist yet

- [ ] **Step 3: Write the minimal generator implementation**

```python
@dataclass(frozen=True, slots=True)
class ManualMixInputConfig:
    aligned_scale: float = 0.62
    wobble_scale: float = 0.18
    opposing_scale: float = 0.55
    vertical_jitter_scale: float = 0.12
    opposing_burst_min_frames: int = 2
    opposing_burst_max_frames: int = 5


@dataclass(frozen=True, slots=True)
class ManualInputFrame:
    frame: int
    manual_right_x: int
    manual_right_y: int
    mode: str
    in_opposing_burst: bool


def generate_manual_mix_frames(manifest, *, manual_seed: int, config: ManualMixInputConfig, sim_frames: int) -> tuple[ManualInputFrame, ...]:
    ...
```

Implementation rules:
- use a stable RNG seed derived from `manifest.scenario_key` and `manual_seed`
- emit deterministic per-frame modes chosen from:
  - `aligned_follow`
  - `corrective_wobble`
  - `opposing_burst`
  - `overshoot_recover`
  - `vertical_jitter`
- ensure at least one eligible burst window occurs for turn/decel scenarios by scheduling burst candidates around event-heavy frames
- keep outputs bounded to `[-32767, 32767]`

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.gamepad.test_gamepad_manual_mix_inputs -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tests/gamepad/manual_mix_inputs.py tests/gamepad/test_gamepad_manual_mix_inputs.py
git commit -m "Add deterministic manual-mix input generator"
```

### Task 2: Add manual-mix metric evaluation and replayable simulation

**Files:**
- Create: `D:\work\AI\yolo-study-001\tests\gamepad\manual_mix_metrics.py`
- Create: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_manual_mix_metrics.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\__init__.py`

- [ ] **Step 1: Write the failing manual-mix metric tests**

```python
import unittest

from tests.gamepad.manual_mix_inputs import ManualInputFrame, ManualMixInputConfig
from tests.gamepad.manual_mix_metrics import (
    ManualMixAggregateMetrics,
    ManualMixMetricsConfig,
    evaluate_manual_mix_run,
    _conflict_frames_ratio,
    _manual_yield_score,
    _wrong_input_recovery_frames,
)
from tests.gamepad.benchmark_scenarios import generate_phase1_manifests


class ManualMixMetricsTests(unittest.TestCase):
    def test_conflict_ratio_counts_only_frames_with_meaningful_opposed_input(self):
        frames = (
            {"manual_x": 6000, "ai_x": -3000, "measured": True},
            {"manual_x": 0, "ai_x": -3000, "measured": True},
            {"manual_x": 6000, "ai_x": 3000, "measured": True},
            {"manual_x": 7000, "ai_x": -1000, "measured": False},
        )

        ratio = _conflict_frames_ratio(frames, min_manual=2000, min_ai=2000)
        self.assertAlmostEqual(ratio, 1.0 / 3.0)

    def test_manual_yield_score_is_higher_when_ai_yields_during_opposing_bursts(self):
        frames = (
            {"manual_x": -6000, "ai_x": 1000, "in_opposing_burst": True},
            {"manual_x": -6000, "ai_x": 500, "in_opposing_burst": True},
        )

        score = _manual_yield_score(frames)
        self.assertGreater(score, 0.7)

    def test_evaluate_manual_mix_run_reuses_phase1_manifests_and_manual_seeds(self):
        manifests = generate_phase1_manifests("mix-suite", 12345)[:2]
        summary = evaluate_manual_mix_run(
            run_key="mix-suite",
            manifests=manifests,
            manual_seeds=(1, 2),
            config=ManualMixMetricsConfig(),
            input_config=ManualMixInputConfig(),
        )

        self.assertEqual(len(summary.scenario_metrics), 4)
        self.assertIsInstance(summary.aggregate, ManualMixAggregateMetrics)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.gamepad.test_gamepad_manual_mix_metrics -v`
Expected: FAIL because `tests.gamepad.manual_mix_metrics` does not exist yet

- [ ] **Step 3: Write the minimal manual-mix evaluator**

```python
@dataclass(frozen=True, slots=True)
class ManualMixMetricsConfig:
    frame_dt: float = 1.0 / 60.0
    sim_frames: int = 180
    measure_from_frame: int = 60
    max_reticle_speed_pps: float = 1500.0
    stick_max: int = 32767
    conflict_manual_threshold: int = 2000
    conflict_ai_threshold: int = 2000
    wrong_input_recovery_threshold_px: float = 8.0
    wrong_input_recovery_consecutive_frames: int = 3


def evaluate_manual_mix_run(...):
    ...
```

Implementation rules:
- reuse expanded target states from `benchmark_scenarios`
- derive AI contribution as `output.right_x - manual_right_x` and `output.right_y - manual_right_y`
- keep the existing tracking metrics from the clean benchmark
- add:
  - `conflict_frames_ratio`
  - `wrong_input_recovery_frames`
  - `manual_yield_score`
- summarize one result per `(manifest, manual_seed)` pair

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.gamepad.test_gamepad_manual_mix_metrics -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tests/gamepad/manual_mix_metrics.py tests/gamepad/test_gamepad_manual_mix_metrics.py tests/gamepad/__init__.py
git commit -m "Add manual-mix benchmark metrics"
```

### Task 3: Route the suite through the benchmark runner and scoreboard

**Files:**
- Modify: `D:\work\AI\yolo-study-001\tools\run_gamepad_benchmark.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\benchmark_scoreboard.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_benchmark_runner.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_benchmark_scoreboard.py`
- Create: `D:\work\AI\yolo-study-001\docs\project\GAMEPAD_MANUAL_MIX_BENCHMARKS.md`

- [ ] **Step 1: Write the failing runner and scoreboard tests**

```python
def test_run_benchmark_with_manual_mix_suite_writes_to_manual_mix_paths(self):
    with TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        artifact_dir = temp_path / "artifacts" / "benchmarks" / "gamepad_manual_mix"
        scoreboard_path = temp_path / "docs" / "project" / "GAMEPAD_MANUAL_MIX_BENCHMARKS.md"

        result = run_benchmark(
            run_key="mix-run",
            run_seed=12345,
            suite="manual-mix",
            artifact_dir=artifact_dir,
            scoreboard_path=scoreboard_path,
            git_metadata=self.git_metadata(),
        )

        self.assertEqual(result["suite"], "manual-mix")
        self.assertTrue((artifact_dir / "mix-run.json").exists())
        self.assertIn("# Gamepad Manual-Mix Benchmarks", scoreboard_path.read_text(encoding="utf-8"))
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.gamepad.test_gamepad_benchmark_runner tests.gamepad.test_gamepad_benchmark_scoreboard -v`
Expected: FAIL because the runner does not yet support a `manual-mix` suite

- [ ] **Step 3: Write the minimal runner and scoreboard changes**

```python
DEFAULT_SUITE = "phase1"
DEFAULT_MANUAL_MIX_ARTIFACT_DIR = PROJECT_ROOT / "artifacts" / "benchmarks" / "gamepad_manual_mix"
DEFAULT_MANUAL_MIX_SCOREBOARD_PATH = PROJECT_ROOT / "docs" / "project" / "GAMEPAD_MANUAL_MIX_BENCHMARKS.md"
```

Implementation rules:
- keep `phase1` as the default suite and preserve existing behavior
- add `suite` metadata into the artifact
- render a manual-mix scoreboard title and parameter section without changing the existing phase1 scoreboard title
- create `docs/project/GAMEPAD_MANUAL_MIX_BENCHMARKS.md` through runner output
- make replay honor the stored suite and artifact layout

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.gamepad.test_gamepad_benchmark_runner tests.gamepad.test_gamepad_benchmark_scoreboard -v`
Expected: PASS

- [ ] **Step 5: Run the focused full verification**

Run: `python -m unittest tests.gamepad.test_gamepad_manual_mix_inputs tests.gamepad.test_gamepad_manual_mix_metrics tests.gamepad.test_gamepad_benchmark_runner tests.gamepad.test_gamepad_benchmark_scoreboard tests.gamepad.test_gamepad_benchmark_metrics -v`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add tools/run_gamepad_benchmark.py tests/gamepad/benchmark_scoreboard.py tests/gamepad/test_gamepad_benchmark_runner.py tests/gamepad/test_gamepad_benchmark_scoreboard.py docs/project/GAMEPAD_MANUAL_MIX_BENCHMARKS.md
git commit -m "Wire manual-mix benchmark suite into runner"
```
