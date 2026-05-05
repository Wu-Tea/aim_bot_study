from __future__ import annotations

import json
from os import PathLike
from pathlib import Path
from typing import Any

from vision.recoil_collection.models import RecoilProfileRecord
from vision.weapon_identity.models import VisualSignatureRecord
from vision.weapon_identity.models import WeaponIdentityRecord

JsonPath = str | PathLike[str]


def save_identity_record(path: JsonPath, record: WeaponIdentityRecord) -> None:
    _write_json(path, record.to_dict())


def load_identity_record(path: JsonPath) -> WeaponIdentityRecord:
    return _load_record(path, WeaponIdentityRecord, "weapon identity record")


def save_signature_record(path: JsonPath, record: VisualSignatureRecord) -> None:
    _write_json(path, record.to_dict())


def load_signature_record(path: JsonPath) -> VisualSignatureRecord:
    return _load_record(path, VisualSignatureRecord, "visual signature record")


def save_recoil_profile(path: JsonPath, record: RecoilProfileRecord) -> None:
    _write_json(path, record.to_dict())


def load_recoil_profile(path: JsonPath) -> RecoilProfileRecord:
    return _load_record(path, RecoilProfileRecord, "recoil profile record")


def _load_record(path: JsonPath, record_type: type[Any], label: str) -> Any:
    file_path = Path(path)
    payload = _read_json(file_path)
    try:
        return record_type.from_dict(payload)
    except ValueError as exc:
        raise ValueError(f"Invalid {label} payload in {file_path}: {exc}") from exc


def _read_json(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON in {path}: {exc.msg}") from exc
    except OSError as exc:
        raise ValueError(f"Unable to read JSON from {path}: {exc}") from exc

    if not isinstance(payload, dict):
        raise ValueError(f"JSON payload in {path} must be an object")

    return payload


def _write_json(path: JsonPath, payload: dict[str, Any]) -> None:
    file_path = Path(path)
    file_path.parent.mkdir(parents=True, exist_ok=True)
    serialized = json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    try:
        file_path.write_text(serialized, encoding="utf-8")
    except OSError as exc:
        raise ValueError(f"Unable to write JSON to {file_path}: {exc}") from exc
