from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path

from vision.recoil_collection.models import RecoilProfileRecord
from vision.recoil_collection.readiness import profile_readiness_reason


@dataclass(slots=True, frozen=True)
class RecoilProfileAudit:
    profile_id: str
    aim_mode: str
    confidence: float
    sample_count: int
    duration_ms: int
    findings: tuple[str, ...]
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
        audits.append(
            RecoilProfileAudit(
                profile_id=profile.profile_id,
                aim_mode=profile.aim_mode,
                confidence=profile.confidence,
                sample_count=profile.sample_count,
                duration_ms=profile.duration_ms,
                findings=findings,
                runtime_ready=not findings,
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
    return tuple(dict.fromkeys(findings))


def _has_vertical_direction_reversal(samples_y: tuple[float, ...]) -> bool:
    if len(samples_y) < 4:
        return False
    return min(samples_y) < -5.0 and max(samples_y) > 5.0


__all__ = [
    "RecoilProfileAudit",
    "RecoilProfileDirectoryAudit",
    "audit_recoil_profile_directory",
]
