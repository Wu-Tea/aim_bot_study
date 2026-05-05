from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Iterable
from typing import TextIO

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.weapon_recognizer import _build_default_recognizer
from vision.recoil_collection.capture import RecoilCollectionError
from vision.recoil_collection.capture import RecoilCollectorConfig
from vision.recoil_collection.capture import build_full_screen_frame_grabber
from vision.recoil_collection.capture import build_live_motion_sampler
from vision.recoil_collection.capture import collect_recoil_profile
from vision.recoil_collection.storage import StorageError
from vision.recoil_collection.storage import save_recoil_profile
from vision.recoil_collection.storage import save_recoil_profile_summary
from vision.weapon_identity.adapters import ADAPTER_REGISTRY


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Collect a recoil profile from repeated standing-fire bursts.")
    parser.add_argument("--game", choices=tuple(sorted(ADAPTER_REGISTRY)), required=True)
    parser.add_argument("--mode", choices=("hipfire", "ads"), required=True)
    parser.add_argument("--standing-only", action="store_true", help="V1 collector currently supports standing only.")
    parser.add_argument("--profile-dir", type=Path, required=True)
    parser.add_argument("--signature-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="Where to write the JSON capture summary.")
    return parser


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    if not args.standing_only:
        parser.error("--standing-only is required in V1")
    return args


def main(
    argv: Iterable[str] | None = None,
    *,
    stdout: TextIO | None = None,
    stderr: TextIO | None = None,
    recognizer_factory=None,
    weapon_frame_grabber_factory=None,
    motion_sampler_factory=None,
    timestamp_fn=None,
) -> int:
    args = parse_args(argv)
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr
    recognizer_factory = recognizer_factory or _build_default_recognizer
    weapon_frame_grabber_factory = weapon_frame_grabber_factory or (lambda args: build_full_screen_frame_grabber())

    config = RecoilCollectorConfig()
    motion_sampler_factory = motion_sampler_factory or (lambda args, config: build_live_motion_sampler(config))
    timestamp_fn = timestamp_fn or _utc_timestamp

    try:
        recognizer = recognizer_factory(args)
        weapon_frame_source = weapon_frame_grabber_factory(args)
        motion_sampler = motion_sampler_factory(args, config)

        result = collect_recoil_profile(
            game=args.game,
            aim_mode=args.mode,
            standing_only=args.standing_only,
            recognizer=recognizer,
            weapon_frame_source=weapon_frame_source,
            motion_sampler=motion_sampler,
            config=config,
            timestamp_fn=timestamp_fn,
        )

        profile_path = args.profile_dir / f"{result.extracted_profile.profile.profile_id}.json"
        profile_summary_path = args.profile_dir / f"{result.profile_summary.profile_id}.summary.json"
        save_recoil_profile(profile_path, result.extracted_profile.profile)
        save_recoil_profile_summary(profile_summary_path, result.profile_summary)

        payload = _build_summary_payload(
            result=result,
            profile_path=profile_path,
            profile_summary_path=profile_summary_path,
        )
        _write_summary_output(args.output, payload)
        stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
        stdout.flush()
        return 0
    except KeyboardInterrupt:
        return 0
    except (FileNotFoundError, OSError, ValueError, json.JSONDecodeError, StorageError, RecoilCollectionError) as exc:
        print(str(exc), file=stderr)
        return 1


def _build_summary_payload(*, result, profile_path: Path, profile_summary_path: Path) -> dict[str, object]:
    return {
        "type": "recoil_collection_summary",
        "session_id": result.session.session_id,
        "collector_version": result.session.collector_version,
        "recognized_weapon": result.recognition_event.to_dict(),
        "profile_summary": result.profile_summary.to_dict(),
        "profile_path": str(profile_path),
        "profile_summary_path": str(profile_summary_path),
        "accepted_burst_ids": list(result.extracted_profile.accepted_burst_ids),
        "rejected_burst_ids": list(result.extracted_profile.rejected_burst_ids),
        "variance_summary": dict(result.extracted_profile.profile.variance_summary),
    }


def _write_summary_output(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _utc_timestamp() -> str:
    from datetime import datetime
    from datetime import timezone

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


if __name__ == "__main__":
    raise SystemExit(main())
