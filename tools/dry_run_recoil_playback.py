from __future__ import annotations

import argparse
from dataclasses import replace
import json
from pathlib import Path
import sys

_REPO_ROOT = Path(__file__).resolve().parents[1]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from config import load_tuning_config
from controllers.gamepad.recoil_compensation import RecoilCompensationConfig
from controllers.gamepad.recoil_compensation import RecoilCompensationPlugin
from controllers.gamepad.state import GamepadFrame
from controllers.gamepad.state import GamepadOutput
from vision.recoil_collection.calibration import RecoilControlCalibration
from vision.recoil_collection.calibration import load_calibration
from vision.recoil_collection.models import RecoilProfileRecord


def simulate_profile_playback(
    profile: RecoilProfileRecord,
    *,
    config: RecoilCompensationConfig,
    calibration: RecoilControlCalibration | None = None,
    frame_count: int | None = None,
    step_ms: int | None = None,
) -> dict:
    resolved_frame_count = frame_count if frame_count is not None else profile.sample_count
    resolved_step_ms = step_ms if step_ms is not None else profile.sample_interval_ms
    if resolved_frame_count <= 0:
        raise ValueError("frame_count must be positive")
    if resolved_step_ms <= 0:
        raise ValueError("step_ms must be positive")

    provider_result = (profile, calibration) if calibration is not None else profile
    plugin = RecoilCompensationPlugin(config, profile_provider=lambda _frame: provider_result)
    frames: list[dict] = []
    base_timestamp = 1.0

    for index in range(resolved_frame_count):
        elapsed_ms = index * resolved_step_ms
        output = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=base_timestamp + (elapsed_ms / 1000.0)), output)
        profile_x, profile_y = _profile_values_at_elapsed_ms(
            profile,
            elapsed_ms=elapsed_ms + max(0, int(config.profile_lead_ms)),
        )
        frames.append(
            {
                "elapsed_ms": elapsed_ms,
                "profile_x": profile_x,
                "profile_y": profile_y,
                "right_x": int(output.right_x),
                "right_y": int(output.right_y),
            }
        )

    return {
        "profile_id": profile.profile_id,
        "canonical_weapon_id": profile.canonical_weapon_id,
        "aim_mode": profile.aim_mode,
        "stance": profile.stance,
        "confidence": float(profile.confidence),
        "sample_interval_ms": int(profile.sample_interval_ms),
        "profile_amount": float(config.profile_amount),
        "profile_x_amount": float(config.profile_x_amount),
        "feedback_amount": float(config.feedback_amount),
        "profile_lead_ms": int(config.profile_lead_ms),
        "profile_velocity_reference_ms": int(config.profile_velocity_reference_ms),
        "calibrated": calibration is not None,
        "frame_count": len(frames),
        "peak_abs_right_x": max(abs(frame["right_x"]) for frame in frames),
        "peak_abs_right_y": max(abs(frame["right_y"]) for frame in frames),
        "frames": frames,
    }


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    profile = _load_profile(args.profile)
    config = _resolve_config(args)
    calibration = load_calibration(args.calibration) if args.calibration is not None else None
    report = simulate_profile_playback(
        profile,
        config=config,
        calibration=calibration,
        frame_count=args.frame_count,
        step_ms=args.step_ms,
    )
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Dry-run a recoil profile through gamepad stick playback")
    parser.add_argument("--profile", required=True, type=Path, help="Path to a recoil profile JSON file")
    parser.add_argument("--config", type=Path, default=None, help="Optional config.toml path")
    parser.add_argument("--calibration", type=Path, default=None, help="Optional recoil calibration JSON file")
    parser.add_argument(
        "--profile-amount",
        type=float,
        default=None,
        help="Override [gamepad.recoil].profile_amount",
    )
    parser.add_argument(
        "--profile-x-amount",
        type=float,
        default=None,
        help="Override [gamepad.recoil].profile_x_amount",
    )
    parser.add_argument("--feedback-amount", type=float, default=None, help="Override fixed fallback down-pull scale")
    parser.add_argument("--profile-lead-ms", type=int, default=None, help="Advance profile playback by this many milliseconds")
    parser.add_argument(
        "--profile-velocity-reference-ms",
        type=int,
        default=None,
        help="Reference window used to map uncalibrated profile deltas into stick output",
    )
    parser.add_argument("--frame-count", type=int, default=None, help="Number of frames to simulate")
    parser.add_argument("--step-ms", type=int, default=None, help="Simulation step in milliseconds")
    return parser.parse_args(argv)


def _load_profile(path: Path) -> RecoilProfileRecord:
    payload = json.loads(path.read_text(encoding="utf-8"))
    return RecoilProfileRecord.from_dict(payload)


def _resolve_config(args: argparse.Namespace) -> RecoilCompensationConfig:
    config = load_tuning_config(args.config).gamepad_recoil
    overrides = {}
    if args.profile_amount is not None:
        overrides["profile_amount"] = float(args.profile_amount)
    if args.profile_x_amount is not None:
        overrides["profile_x_amount"] = float(args.profile_x_amount)
    if args.feedback_amount is not None:
        overrides["feedback_amount"] = float(args.feedback_amount)
    if args.profile_lead_ms is not None:
        overrides["profile_lead_ms"] = int(args.profile_lead_ms)
    if args.profile_velocity_reference_ms is not None:
        overrides["profile_velocity_reference_ms"] = int(args.profile_velocity_reference_ms)
    return replace(config, **overrides) if overrides else config


def _frame(*, timestamp: float) -> GamepadFrame:
    return GamepadFrame(
        timestamp=timestamp,
        left_x=0,
        left_y=0,
        manual_right_x=0,
        manual_right_y=0,
        left_trigger=255,
        right_trigger=255,
        buttons={"rb": False},
        is_aiming=True,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=False,
    )


def _profile_values_at_elapsed_ms(profile: RecoilProfileRecord, *, elapsed_ms: int) -> tuple[float, float]:
    if elapsed_ms < profile.initial_delay_ms:
        return 0.0, 0.0
    sample_index = (elapsed_ms - profile.initial_delay_ms) // profile.sample_interval_ms
    sample_index = max(0, min(sample_index, profile.sample_count - 1))
    return float(profile.samples_x[sample_index]), float(profile.samples_y[sample_index])


if __name__ == "__main__":
    raise SystemExit(main())
