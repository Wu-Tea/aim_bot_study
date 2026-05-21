import csv
import io
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from tools.analyze_mouse_telemetry import health_failures, main, summarize


HEADER = (
    "timestamp",
    "is_aiming",
    "manual_dx",
    "manual_dy",
    "manual_override_active",
    "manual_left_pressed",
    "target_dx",
    "target_dy",
    "target_revision",
    "target_source",
    "target_age_ms",
    "vision_handoff_ms",
    "controller_consume_age_ms",
    "ai_mode",
    "ai_phase",
    "output_dx",
    "output_dy",
    "move_x",
    "move_y",
    "left_click",
    "auto_fire_active",
    "inject_remainder_dx",
    "inject_remainder_dy",
    "pending_injected_dx",
    "pending_injected_dy",
    "local_motion_dx_since_target",
    "local_motion_dy_since_target",
    "physical_right_pressed",
    "physical_aim_sync_resets",
    "physical_aim_sync_presses",
    "injection_errors",
)


def _row(
    *,
    timestamp,
    revision=1,
    target_source="observed",
    phase="snap",
    output_dx=0.0,
    move_x=0,
    target_dx=72.0,
    target_dy=0.0,
    manual_override=0,
    manual_left=0,
    is_aiming=1,
    physical_right=1,
    sync_resets=0,
    sync_presses=0,
    injection_errors=0,
    target_age_ms=0.0,
    vision_handoff_ms=0.0,
    controller_consume_age_ms=0.0,
    manual_dx=0.0,
    manual_dy=0.0,
    pending_injected_dx=0.0,
    pending_injected_dy=0.0,
):
    values = {
        "timestamp": f"{timestamp:.3f}",
        "is_aiming": str(is_aiming),
        "manual_dx": f"{manual_dx:.3f}",
        "manual_dy": f"{manual_dy:.3f}",
        "manual_override_active": str(manual_override),
        "manual_left_pressed": str(manual_left),
        "target_dx": f"{target_dx:.3f}",
        "target_dy": f"{target_dy:.3f}",
        "target_revision": str(revision),
        "target_source": target_source,
        "target_age_ms": f"{target_age_ms:.3f}",
        "vision_handoff_ms": f"{vision_handoff_ms:.3f}",
        "controller_consume_age_ms": f"{controller_consume_age_ms:.3f}",
        "ai_mode": "acquire_far",
        "ai_phase": phase,
        "output_dx": str(output_dx),
        "output_dy": "0",
        "move_x": str(move_x),
        "move_y": "0",
        "left_click": "0",
        "auto_fire_active": "0",
        "inject_remainder_dx": "0",
        "inject_remainder_dy": "0",
        "pending_injected_dx": f"{pending_injected_dx:.3f}",
        "pending_injected_dy": f"{pending_injected_dy:.3f}",
        "local_motion_dx_since_target": "0",
        "local_motion_dy_since_target": "0",
        "physical_right_pressed": str(physical_right),
        "physical_aim_sync_resets": str(sync_resets),
        "physical_aim_sync_presses": str(sync_presses),
        "injection_errors": str(injection_errors),
    }
    return [values[key] for key in HEADER]


class MouseTelemetryAnalysisTests(unittest.TestCase):
    def _write_csv(self, rows):
        temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        path = Path(temp_dir.name) / "mouse-controller-test.csv"
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(HEADER)
            writer.writerows(rows)
        return path

    def test_summary_detects_continuous_output_on_repeated_target_revision(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, revision=3, output_dx=5.0, move_x=5),
                _row(timestamp=1.001, revision=3, output_dx=0.8, move_x=1),
                _row(timestamp=1.002, revision=3, output_dx=0.7, move_x=1),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.target_rows, 3)
        self.assertEqual(summary.nonzero_output_rows, 3)
        self.assertEqual(summary.repeated_revision_output_rows, 2)
        self.assertIn("Controller emits mouse moves", summary.diagnosis[0])
        self.assertEqual(health_failures(summary), [])

    def test_summary_flags_large_same_revision_move_burst(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, revision=3, output_dx=120.0, move_x=120),
                _row(timestamp=1.001, revision=3, output_dx=120.0, move_x=120),
                _row(timestamp=1.002, revision=3, output_dx=0.0, move_x=0),
            ]
        )

        summary = summarize(path)

        self.assertAlmostEqual(summary.max_same_revision_move_burst, 240.0)
        self.assertEqual(summary.same_revision_move_rows, 1)
        self.assertIn("same target revision", " ".join(summary.diagnosis))
        self.assertIn("same target revision", " ".join(health_failures(summary)))

    def test_summary_reports_controller_and_mouse_move_cadence(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, revision=3, output_dx=5.0, move_x=5),
                _row(timestamp=1.008, revision=3, output_dx=0.8, move_x=1),
                _row(timestamp=1.016, revision=3, output_dx=0.7, move_x=1),
            ]
        )

        summary = summarize(path)

        self.assertAlmostEqual(summary.controller_loop_hz, 125.0, delta=0.1)
        self.assertAlmostEqual(summary.mouse_move_hz, 125.0, delta=0.1)
        self.assertAlmostEqual(summary.mouse_move_gap_p95_ms, 8.0, delta=0.1)
        self.assertAlmostEqual(summary.mouse_move_gap_max_ms, 8.0, delta=0.1)

    def test_summary_reports_physical_aim_sync_fallback_counts(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, revision=3, output_dx=5.0, move_x=5),
                _row(
                    timestamp=1.008,
                    revision=3,
                    output_dx=0.8,
                    move_x=1,
                    physical_right=0,
                    sync_resets=1,
                ),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.physical_aim_sync_resets, 1)
        self.assertEqual(summary.physical_aim_sync_presses, 0)
        self.assertIn("physical right-button fail-closed", " ".join(summary.diagnosis))

    def test_summary_flags_mouse_injection_errors(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, revision=3, output_dx=5.0, move_x=5),
                _row(
                    timestamp=1.008,
                    revision=3,
                    output_dx=0.8,
                    move_x=0,
                    injection_errors=2,
                ),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.injection_errors, 2)
        self.assertIn("Mouse injection failures", " ".join(summary.diagnosis))
        self.assertIn("Mouse injection failures", " ".join(health_failures(summary)))

    def test_summary_reports_manual_left_pressed_rows(self):
        path = self._write_csv(
            [
                _row(
                    timestamp=1.000,
                    revision=3,
                    output_dx=5.0,
                    move_x=5,
                    manual_left=1,
                    physical_right=0,
                ),
                _row(
                    timestamp=1.008,
                    revision=3,
                    output_dx=0.8,
                    move_x=1,
                    manual_left=1,
                    physical_right=0,
                ),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.manual_left_pressed_rows, 2)

    def test_summary_flags_button_state_not_reaching_controller(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, is_aiming=0, target_source="", phase="manual"),
                _row(timestamp=1.008, is_aiming=0, target_source="", phase="manual"),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.aiming_rows, 0)
        self.assertIn("button state", summary.diagnosis[0])
        self.assertIn("button state", health_failures(summary)[0])

    def test_summary_flags_stale_target_age(self):
        path = self._write_csv(
            [
                _row(
                    timestamp=1.000,
                    revision=3,
                    output_dx=5.0,
                    move_x=5,
                    target_age_ms=120.0,
                    controller_consume_age_ms=20.0,
                ),
                _row(
                    timestamp=1.008,
                    revision=3,
                    output_dx=0.8,
                    move_x=1,
                    target_age_ms=140.0,
                    controller_consume_age_ms=24.0,
                ),
            ]
        )

        summary = summarize(path)

        self.assertGreater(summary.target_age_p95_ms, 130.0)
        self.assertIn("target age", " ".join(summary.diagnosis))
        self.assertIn("target age", " ".join(health_failures(summary)))

    def test_summary_ignores_stale_target_age_outside_active_aiming_target(self):
        path = self._write_csv(
            [
                _row(
                    timestamp=1.000,
                    is_aiming=0,
                    target_source="observed",
                    phase="manual",
                    target_age_ms=5000.0,
                ),
                _row(
                    timestamp=1.008,
                    revision=4,
                    output_dx=0.8,
                    move_x=1,
                    target_age_ms=12.0,
                ),
                _row(
                    timestamp=1.016,
                    revision=4,
                    output_dx=0.8,
                    move_x=1,
                    target_age_ms=14.0,
                ),
            ]
        )

        summary = summarize(path)

        self.assertLess(summary.target_age_p95_ms, 100.0)
        self.assertNotIn("target age", " ".join(health_failures(summary)))

    def test_summary_ignores_stale_consume_age_outside_active_aiming_target(self):
        path = self._write_csv(
            [
                _row(
                    timestamp=1.000,
                    is_aiming=0,
                    target_source="observed",
                    phase="manual",
                    controller_consume_age_ms=5000.0,
                ),
                _row(
                    timestamp=1.008,
                    revision=4,
                    output_dx=0.8,
                    move_x=1,
                    controller_consume_age_ms=8.0,
                ),
                _row(
                    timestamp=1.016,
                    revision=4,
                    output_dx=0.8,
                    move_x=1,
                    controller_consume_age_ms=10.0,
                ),
            ]
        )

        summary = summarize(path)

        self.assertLess(summary.controller_consume_age_p95_ms, 100.0)

    def test_summary_flags_reverse_injected_echo_reaching_manual_arbitration(self):
        path = self._write_csv(
            [
                _row(
                    timestamp=1.000,
                    revision=3,
                    output_dx=5.0,
                    move_x=5,
                    manual_dx=11.0,
                    pending_injected_dx=-24.0,
                    manual_override=1,
                ),
                _row(
                    timestamp=1.008,
                    revision=3,
                    output_dx=0.8,
                    move_x=1,
                    manual_dx=10.0,
                    pending_injected_dx=-12.0,
                    manual_override=1,
                ),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.reverse_injected_echo_rows, 2)
        self.assertIn("Synthetic mouse echo", " ".join(summary.diagnosis))
        self.assertIn("Synthetic mouse echo", " ".join(health_failures(summary)))

    def test_summary_flags_manual_trigger_immediately_after_injected_move(self):
        path = self._write_csv(
            [
                _row(
                    timestamp=1.000,
                    revision=3,
                    output_dx=-24.0,
                    move_x=-24,
                    pending_injected_dx=-24.0,
                ),
                _row(
                    timestamp=1.002,
                    revision=3,
                    phase="manual",
                    output_dx=0.0,
                    move_x=0,
                    manual_dx=11.0,
                    manual_override=1,
                ),
                _row(
                    timestamp=1.004,
                    revision=3,
                    output_dx=0.8,
                    move_x=1,
                ),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.injected_echo_manual_rows, 1)
        self.assertIn("recent injected move", " ".join(summary.diagnosis))
        self.assertIn("recent injected move", " ".join(health_failures(summary)))

    def test_health_flags_far_targets_without_snap_phase(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, revision=3, phase="acquire", output_dx=6.0, move_x=6),
                _row(timestamp=1.008, revision=3, phase="acquire", output_dx=2.0, move_x=2),
                _row(timestamp=1.016, revision=3, phase="body_lock", output_dx=1.0, move_x=1),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.snap_rows, 0)
        self.assertGreater(summary.max_target_radius_px, 32.0)
        self.assertIn("snap phase never engaged", " ".join(summary.diagnosis))
        self.assertIn("snap phase never engaged", " ".join(health_failures(summary)))

    def test_health_flags_controller_loop_running_below_mouse_target_cadence(self):
        path = self._write_csv(
            [
                _row(
                    timestamp=1.000 + index * (1.0 / 60.0),
                    revision=3,
                    output_dx=1.0,
                    move_x=1,
                )
                for index in range(20)
            ]
        )

        summary = summarize(path)

        self.assertLess(summary.controller_loop_hz, 90.0)
        self.assertIn("controller loop cadence", " ".join(health_failures(summary)))

    def test_summary_flags_ai_output_without_integer_mouse_moves(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, revision=3, output_dx=0.4, move_x=0),
                _row(timestamp=1.001, revision=3, output_dx=0.4, move_x=0),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.nonzero_move_rows, 0)
        self.assertIn("no integer mouse move", summary.diagnosis[0])
        self.assertIn("no integer mouse move", health_failures(summary)[0])

    def test_summary_flags_missing_targets(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, target_source="", phase="manual"),
                _row(timestamp=1.001, target_source="", phase="manual"),
            ]
        )

        summary = summarize(path)

        self.assertEqual(summary.target_rows, 0)
        self.assertIn("No controller target rows", summary.diagnosis[0])
        self.assertIn("No controller target rows", health_failures(summary)[0])

    def test_assert_healthy_cli_returns_nonzero_for_unhealthy_run(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, revision=3, output_dx=0.4, move_x=0),
                _row(timestamp=1.001, revision=3, output_dx=0.4, move_x=0),
            ]
        )

        with redirect_stdout(io.StringIO()):
            result = main([str(path), "--assert-healthy"])

        self.assertEqual(result, 2)

    def test_assert_healthy_cli_returns_zero_for_healthy_run(self):
        path = self._write_csv(
            [
                _row(timestamp=1.000, revision=3, output_dx=5.0, move_x=5),
                _row(timestamp=1.001, revision=3, output_dx=0.8, move_x=1),
                _row(timestamp=1.002, revision=3, output_dx=0.7, move_x=1),
            ]
        )

        with redirect_stdout(io.StringIO()):
            result = main([str(path), "--assert-healthy"])

        self.assertEqual(result, 0)

    def test_mouse_native_debug_analyzes_only_current_run_telemetry(self):
        script = Path(__file__).resolve().parents[2] / "mouse_native_debug.bat"
        content = script.read_text(encoding="utf-8")

        self.assertIn("MOUSE_TELEMETRY_PATH", content)
        self.assertIn('"%MOUSE_TELEMETRY_PATH%" --assert-healthy', content)


if __name__ == "__main__":
    unittest.main()
