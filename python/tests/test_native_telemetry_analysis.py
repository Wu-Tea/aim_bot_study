import json
from pathlib import Path

from tools.analyze_ads_transition_model import analyze_ads_transitions
from tools.analyze_native_user_profile import analyze_user_profile


def _write(path: Path, rows: list[dict]) -> None:
    path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")


def test_user_profile_reports_manual_ai_conflict_and_shadow_readiness(tmp_path):
    log = tmp_path / "telemetry.jsonl"
    rows = []
    for index in range(120):
        manual = -0.4 if index >= 20 else 0.0
        rows.append({
            "type": "controller_sample",
            "sample_ns": index * 4_000_000,
            "manual_x": manual,
            "manual_y": 0.0,
            "ai_x": 0.35 if index >= 20 else 0.0,
            "ai_y": 0.0,
            "pre_recoil_x": 0.05 if index >= 20 else 0.0,
            "pre_recoil_y": 0.0,
            "aim_mode": "body_lock",
            "manual_takeover_active": index >= 30,
        })
    _write(log, rows)

    report = analyze_user_profile([log])
    assert report["schema"] == "cod_user_profile_v1"
    assert report["controller_samples"] == 120
    assert report["manual_ai_conflict_samples"] == 100
    assert report["manual_takeover_samples"] == 90
    assert report["direction_preservation_ratio"] < 0.5
    assert report["shadow_only"] is True
    assert report["ready_for_live_adaptation"] is False


def test_ads_model_uses_only_clean_complete_samples_and_exposes_gates(tmp_path):
    log = tmp_path / "telemetry.jsonl"
    rows = []
    for index in range(12):
        rows.append({
            "type": "ads_transition",
            "valid": True,
            "complete": True,
            "calibration_class": "calibration_clean" if index < 10 else "conditional_model",
            "scale_x": 1.2 + index * 0.001,
            "scale_y": 1.1 + index * 0.001,
            "offset_x": 2.0,
            "offset_y": -1.0,
            "settle_confidence": 0.9,
        })
    rows.append({"type": "ads_transition", "valid": False, "complete": True})
    _write(log, rows)

    report = analyze_ads_transitions([log])
    assert report["schema"] == "cod_ads_transition_model_v1"
    assert report["valid_complete_samples"] == 12
    assert report["clean_samples"] == 10
    assert 1.20 <= report["clean_model"]["scale_x"]["median"] <= 1.21
    assert report["ready_for_tracker_calibration"] is False
    assert "clean_samples<50" in report["readiness_reasons"]
