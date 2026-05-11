from __future__ import annotations

import argparse
from datetime import datetime
from datetime import timezone
import json
from pathlib import Path
import sys
import time
from typing import Any
from typing import Iterable
from typing import TextIO

import cv2
import numpy as np

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from vision.recoil_collection.capture import build_full_screen_frame_grabber
from vision.recoil_collection.storage import StorageError
from vision.recoil_collection.storage import load_identity_record
from vision.recoil_collection.storage import save_identity_record
from vision.recoil_collection.storage import save_signature_record
from vision.weapon_identity.adapters import ADAPTER_REGISTRY
from vision.weapon_identity.adapters import NormalizedROI
from vision.weapon_identity.adapters import get_adapter
from vision.weapon_identity.models import VisualSignatureRecord
from vision.weapon_identity.models import WeaponIdentityRecord
from vision.weapon_identity.signatures import CLASSICAL_SIGNATURE_FEATURE_TYPE
from vision.weapon_identity.signatures import extract_signature


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Capture and save a COD weapon signature from a HUD frame.")
    parser.add_argument("--game", choices=tuple(sorted(ADAPTER_REGISTRY)), required=True)
    parser.add_argument("--canonical-weapon-id", required=True)
    parser.add_argument("--display-name", required=True)
    parser.add_argument("--weapon-family", required=True)
    parser.add_argument("--signature-dir", type=Path, required=True)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--notes", default="")
    parser.add_argument("--resolution-bucket")
    parser.add_argument("--ui-scale-bucket", default="default")
    parser.add_argument("--confidence", type=float, default=0.95)
    parser.add_argument("--identity-only", action="store_true")
    parser.add_argument("--live-capture-delay", type=float, default=3.0)
    return parser


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    if not args.identity_only and args.confidence <= 0.0:
        parser.error("--confidence must be positive")
    if args.live_capture_delay < 0.0:
        parser.error("--live-capture-delay must be zero or positive")
    if args.image is not None and not args.image.exists():
        parser.error(f"--image does not exist: {args.image}")
    return args


def main(
    argv: Iterable[str] | None = None,
    *,
    stdout: TextIO | None = None,
    stderr: TextIO | None = None,
    frame_grabber_factory=None,
    timestamp_fn=None,
    sleep_fn=None,
) -> int:
    args = parse_args(argv)
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr
    frame_grabber_factory = frame_grabber_factory or (lambda: build_full_screen_frame_grabber())
    timestamp_fn = timestamp_fn or _utc_timestamp
    sleep_fn = sleep_fn or time.sleep

    try:
        adapter = get_adapter(args.game)
        captured_at = timestamp_fn()
        identity_path = args.signature_dir / f"identity-{args.game}-{args.canonical_weapon_id}.json"
        existing_identity = _load_existing_identity(identity_path)

        signature_record = None
        crop_rgb = None
        crop_path = None
        if not args.identity_only:
            frame_rgb = _load_source_frame(
                args,
                frame_grabber_factory,
                stderr=stderr,
                sleep_fn=sleep_fn,
            )
            crop_rgb = _crop_normalized_roi(frame_rgb, adapter.weapon_icon_roi)
            if crop_rgb.size == 0:
                raise ValueError("Weapon icon ROI was empty; unable to capture signature")
            signature_record = _build_signature_record(
                args=args,
                captured_at=captured_at,
                frame_rgb=frame_rgb,
                crop_rgb=crop_rgb,
            )
            signature_path = args.signature_dir / f"{signature_record.signature_id}.json"
            crop_path = args.signature_dir / "crops" / f"{signature_record.signature_id}.png"
            save_signature_record(signature_path, signature_record)
            _write_crop_image(crop_path, crop_rgb)
        else:
            signature_path = None

        identity_record = _build_identity_record(
            args=args,
            captured_at=captured_at,
            existing_identity=existing_identity,
            signature_record=signature_record,
        )
        save_identity_record(identity_path, identity_record)

        payload = {
            "type": "weapon_signature_capture",
            "game": args.game,
            "canonical_weapon_id": args.canonical_weapon_id,
            "signature_id": None if signature_record is None else signature_record.signature_id,
            "signature_path": None if signature_path is None else str(signature_path),
            "identity_path": str(identity_path),
            "crop_path": None if crop_path is None else str(crop_path),
            "captured_at": captured_at,
        }
        stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
        stdout.flush()
        return 0
    except (FileNotFoundError, OSError, ValueError, StorageError, json.JSONDecodeError) as exc:
        print(str(exc), file=stderr)
        return 1


def _build_signature_record(
    *,
    args: argparse.Namespace,
    captured_at: str,
    frame_rgb: np.ndarray,
    crop_rgb: np.ndarray,
) -> VisualSignatureRecord:
    signature_id = f"signature-{args.game}-{args.canonical_weapon_id}-{_compact_timestamp(captured_at)}"
    frame_height, frame_width = frame_rgb.shape[:2]
    resolution_bucket = args.resolution_bucket or f"{frame_width}x{frame_height}"
    extracted_signature = extract_signature(_rgb_to_bgr(crop_rgb))
    return VisualSignatureRecord(
        signature_id=signature_id,
        canonical_weapon_id=args.canonical_weapon_id,
        game=args.game,
        region_type="weapon_icon",
        resolution_bucket=resolution_bucket,
        ui_scale_bucket=args.ui_scale_bucket,
        feature_type=CLASSICAL_SIGNATURE_FEATURE_TYPE,
        feature_payload=extracted_signature.to_feature_payload(),
        captured_from=str(args.image) if args.image is not None else "live_fullscreen_capture",
        confidence=float(args.confidence),
    )


def _build_identity_record(
    *,
    args: argparse.Namespace,
    captured_at: str,
    existing_identity: WeaponIdentityRecord | None,
    signature_record: VisualSignatureRecord | None,
) -> WeaponIdentityRecord:
    signature_refs = list(existing_identity.signature_refs) if existing_identity is not None else []
    if signature_record is not None and signature_record.signature_id not in signature_refs:
        signature_refs.append(signature_record.signature_id)

    alias_names = () if existing_identity is None else existing_identity.alias_names
    blueprint_names = () if existing_identity is None else existing_identity.blueprint_names
    notes = args.notes if args.notes else ("" if existing_identity is None else existing_identity.notes)
    created_at = captured_at if existing_identity is None else existing_identity.created_at

    return WeaponIdentityRecord(
        canonical_weapon_id=args.canonical_weapon_id,
        game=args.game,
        weapon_family=args.weapon_family,
        display_name=args.display_name,
        alias_names=alias_names,
        blueprint_names=blueprint_names,
        signature_refs=tuple(signature_refs),
        notes=notes,
        created_at=created_at,
        updated_at=captured_at,
    )


def _load_existing_identity(path: Path) -> WeaponIdentityRecord | None:
    if not path.exists():
        return None
    return load_identity_record(path)


def _load_source_frame(
    args: argparse.Namespace,
    frame_grabber_factory,
    *,
    stderr: TextIO,
    sleep_fn,
) -> np.ndarray:
    if args.image is not None:
        frame_bgr = cv2.imread(str(args.image), cv2.IMREAD_COLOR)
        if frame_bgr is None:
            raise FileNotFoundError(f"Unable to read image from {args.image}")
        return cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)

    if args.live_capture_delay > 0.0:
        stderr.write(
            f"Switch back to the game now. Capturing live HUD in {args.live_capture_delay:.1f} seconds...\n"
        )
        stderr.flush()
        sleep_fn(float(args.live_capture_delay))

    frame_grabber = frame_grabber_factory()
    try:
        frame = frame_grabber.grab()
    finally:
        close = getattr(frame_grabber, "close", None)
        if callable(close):
            close()
    frame_rgb = np.asarray(frame)
    if frame_rgb.size == 0:
        raise ValueError("Live frame grabber returned an empty frame")
    return frame_rgb


def _crop_normalized_roi(frame: np.ndarray, roi: NormalizedROI) -> np.ndarray:
    height, width = frame.shape[:2]
    left = max(0, min(width, int(round(roi.left * width))))
    top = max(0, min(height, int(round(roi.top * height))))
    right = max(left, min(width, int(round((roi.left + roi.width) * width))))
    bottom = max(top, min(height, int(round((roi.top + roi.height) * height))))
    return frame[top:bottom, left:right]


def _write_crop_image(path: Path, crop_rgb: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    success = cv2.imwrite(str(path), _rgb_to_bgr(crop_rgb))
    if not success:
        raise OSError(f"Unable to write crop image to {path}")


def _rgb_to_bgr(image: np.ndarray) -> np.ndarray:
    if image.ndim == 3 and image.shape[2] >= 3:
        return image[:, :, :3][:, :, ::-1]
    return image


def _compact_timestamp(timestamp: str) -> str:
    return timestamp.replace("-", "").replace(":", "").lower()


def _utc_timestamp() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


if __name__ == "__main__":
    raise SystemExit(main())
