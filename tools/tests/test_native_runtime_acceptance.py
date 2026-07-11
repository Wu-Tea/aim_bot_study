import json
from pathlib import Path

from tools.native_runtime_acceptance import median_run, percentile, score_acceptance


def test_percentile_reports_nearest_rank_values():
    values = list(range(1, 101))
    assert percentile(values, 0.50) == 50
    assert percentile(values, 0.95) == 95
    assert percentile(values, 0.99) == 99


def test_median_run_uses_metric_order_not_input_order():
    runs = [{"p95": 30}, {"p95": 10}, {"p95": 20}]
    assert median_run(runs, "p95")["p95"] == 20


def test_missing_hardware_evidence_is_unverified_not_passed(tmp_path: Path):
    report = score_acceptance({"configuration": {"passed": True}})
    assert report["sections"]["A"]["status"] == "PASS"
    assert report["sections"]["G"]["status"] == "UNVERIFIED"
    assert report["release_decision"] == "UNVERIFIED"


def test_hard_failure_cannot_be_offset_by_performance_gain():
    report = score_acceptance(
        {
            "configuration": {"passed": True},
            "vision": {"passed": True},
            "telemetry": {"passed": True},
            "scheduler": {"passed": True},
            "color": {"passed": True},
            "non_regression": {"passed": False, "reason": "authority mismatch"},
        }
    )
    assert report["sections"]["F"]["status"] == "FAIL"
    assert report["release_decision"] == "FAIL"
