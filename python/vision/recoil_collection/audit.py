from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path

from vision.recoil_collection.models import RecoilProfileRecord
from vision.recoil_collection.readiness import has_vertical_recovery_tail
from vision.recoil_collection.readiness import profile_readiness_reason


@dataclass(slots=True, frozen=True)
class RecoilProfileAudit:
    profile_id: str
    aim_mode: str
    confidence: float
    sample_count: int
    duration_ms: int
    findings: tuple[str, ...]
    diagnostics: dict[str, float]
    runtime_ready: bool


@dataclass(slots=True, frozen=True)
class RecoilProfileDirectoryAudit:
    root: Path
    profiles: tuple[RecoilProfileAudit, ...]


def audit_recoil_profile_directory(root: Path) -> RecoilProfileDirectoryAudit:
    audits: list[RecoilProfileAudit] = []
    for path in sorted(Path(root).glob("*.json")):
        if path.name.endswith(".summary.json"):
            continue
        try:
            profile = RecoilProfileRecord.from_dict(json.loads(path.read_text(encoding="utf-8")))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError):
            continue
        findings = _profile_findings(profile)
        diagnostics = _profile_diagnostics(profile)
        audits.append(
            RecoilProfileAudit(
                profile_id=profile.profile_id,
                aim_mode=profile.aim_mode,
                confidence=profile.confidence,
                sample_count=profile.sample_count,
                duration_ms=profile.duration_ms,
                findings=findings,
                diagnostics=diagnostics,
                runtime_ready=True,
            )
        )
    return RecoilProfileDirectoryAudit(root=Path(root), profiles=tuple(audits))


def _profile_findings(profile: RecoilProfileRecord) -> tuple[str, ...]:
    findings: list[str] = []
    readiness = profile_readiness_reason(profile)
    if readiness is not None:
        findings.append(readiness)
    if profile.profile_type == "magazine_curve_v1" and profile.support_counts and min(profile.support_counts) < 2:
        findings.append("support_below_min")
    if _has_vertical_direction_reversal(profile.samples_y):
        findings.append("vertical_direction_reversal")
    if has_vertical_recovery_tail(profile):
        findings.append("vertical_recovery_tail")
    return tuple(dict.fromkeys(findings))


def _has_vertical_direction_reversal(samples_y: tuple[float, ...]) -> bool:
    if len(samples_y) < 4:
        return False
    return min(samples_y) < -5.0 and max(samples_y) > 5.0


def _profile_diagnostics(profile: RecoilProfileRecord) -> dict[str, float]:
    samples_x = profile.samples_x
    samples_y = profile.samples_y
    diagnostics = {
        "horizontal_peak_abs": max(abs(value) for value in samples_x) if samples_x else 0.0,
        "horizontal_final": float(samples_x[-1]) if samples_x else 0.0,
        "vertical_peak_abs": 0.0,
        "vertical_final_abs": 0.0,
        "vertical_recovery_pixels": 0.0,
        "vertical_recovery_ratio": 1.0,
    }
    if samples_y:
        min_y = min(samples_y)
        max_y = max(samples_y)
        if abs(max_y) >= abs(min_y):
            peak_abs = float(abs(max_y))
            final_abs = float(samples_y[-1])
        else:
            peak_abs = float(abs(min_y))
            final_abs = float(abs(samples_y[-1]))
        final_abs = max(0.0, final_abs)
        diagnostics["vertical_peak_abs"] = peak_abs
        diagnostics["vertical_final_abs"] = final_abs
        if peak_abs > 0.0:
            diagnostics["vertical_recovery_pixels"] = peak_abs - final_abs
            diagnostics["vertical_recovery_ratio"] = final_abs / peak_abs

    for key in (
        "accepted_episode_count",
        "episode_count",
        "horizontal_final_range",
        "vertical_recovery_pixels",
        "vertical_recovery_ratio",
    ):
        value = _float_from_mapping(profile.fit_summary, key)
        if value is not None:
            diagnostics[key] = value
    return diagnostics


def _float_from_mapping(mapping: object, key: str) -> float | None:
    try:
        value = mapping.get(key)
    except AttributeError:
        return None
    if type(value) not in {int, float}:
        return None
    return float(value)


__all__ = [
    "RecoilProfileAudit",
    "RecoilProfileDirectoryAudit",
    "audit_recoil_profile_directory",
]
