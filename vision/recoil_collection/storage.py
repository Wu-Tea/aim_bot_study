from __future__ import annotations

import json
from os import PathLike
from pathlib import Path
from typing import Any

from vision.recoil_collection.models import RecoilProfileRecord
from vision.weapon_identity.models import VisualSignatureRecord
from vision.weapon_identity.models import WeaponIdentityRecord

JsonPath = str | PathLike[str]


class StorageError(ValueError):
    """Raised when JSON storage read/write operations fail."""


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
        raise StorageError(f"Invalid {label} payload in {file_path}: {exc}") from exc


def _read_json(path: Path) -> dict[str, Any]:
    try:
        raw_bytes = path.read_bytes()
    except OSError as exc:
        raise StorageError(f"Unable to read JSON from {path}: {exc}") from exc

    try:
        text = raw_bytes.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise StorageError(f"Invalid UTF-8 in {path}: {exc}") from exc

    try:
        payload = json.loads(text)
    except json.JSONDecodeError as exc:
        raise StorageError(
            f"Invalid JSON in {path}: {exc.msg} (line {exc.lineno} column {exc.colno} char {exc.pos})"
        ) from exc

    if not isinstance(payload, dict):
        raise StorageError(f"JSON payload in {path} must be an object")

    return payload


def _write_json(path: JsonPath, payload: dict[str, Any]) -> None:
    file_path = Path(path)
    try:
        file_path.parent.mkdir(parents=True, exist_ok=True)
        serialized = json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
        file_path.write_bytes(serialized.encode("utf-8"))
    except (TypeError, ValueError) as exc:
        raise StorageError(f"Unable to serialize JSON for {file_path}: {exc}") from exc
    except OSError as exc:
        raise StorageError(f"Unable to write JSON to {file_path}: {exc}") from exc
