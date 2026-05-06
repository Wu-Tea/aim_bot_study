from __future__ import annotations

import argparse
from dataclasses import dataclass
from dataclasses import field
from datetime import datetime
from datetime import timezone
import json
from pathlib import Path
import sys
from typing import Any
from typing import Iterable
from typing import TextIO

import win32api

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from vision.capture import ScreenCaptureThread
from vision.recoil_collection.models import RecoilProfileRecord
from vision.weapon_identity.adapters import ADAPTER_REGISTRY
from vision.weapon_identity.adapters import NormalizedROI
from vision.weapon_identity.adapters import get_adapter
from vision.weapon_identity.models import RecognitionEvent
from vision.weapon_identity.models import VisualSignatureRecord
from vision.weapon_identity.models import WeaponIdentityRecord
from vision.weapon_identity.resolver import resolve_weapon
from vision.weapon_identity.runtime_state import ResolverRuntimeState
from vision.weapon_identity.signatures import score_candidates
from vision.weapon_identity.text import extract_text_candidates

_IMAGE_SWITCH_SCORE_THRESHOLD = 0.90
_IMAGE_SWITCH_MARGIN_THRESHOLD = 0.08
_IDENTITY_HINT_KEYS = frozenset(
    {
        "weapon_family",
        "display_name",
        "alias_names",
        "blueprint_names",
        "signature_refs",
        "notes",
        "updated_at",
    }
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Recognize the current COD weapon from the HUD.")
    parser.add_argument("--game", choices=tuple(sorted(ADAPTER_REGISTRY)), required=True)
    parser.add_argument("--profile-dir", type=Path, required=True)
    parser.add_argument("--signature-dir", type=Path, required=True)
    parser.add_argument("--capture-mode", choices=("fullscreen", "center", "region"), default="fullscreen")
    parser.add_argument("--capture-left", type=int)
    parser.add_argument("--capture-top", type=int)
    parser.add_argument("--capture-width", type=int)
    parser.add_argument("--capture-height", type=int)
    parser.add_argument("--fps", type=int, required=True)
    parser.add_argument(
        "--latest-state-file",
        type=Path,
        help="Optional JSON file that receives the latest emitted current-weapon state.",
    )
    return parser


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    if args.fps <= 0:
        parser.error("--fps must be positive")
    if args.capture_width is not None and args.capture_width <= 0:
        parser.error("--capture-width must be positive")
    if args.capture_height is not None and args.capture_height <= 0:
        parser.error("--capture-height must be positive")
    if args.capture_left is not None and args.capture_left < 0:
        parser.error("--capture-left must be non-negative")
    if args.capture_top is not None and args.capture_top < 0:
        parser.error("--capture-top must be non-negative")
    if args.capture_mode in {"center", "region"}:
        if args.capture_width is None or args.capture_height is None:
            parser.error("--capture-width and --capture-height are required for center or region capture mode")
    if args.capture_mode == "region":
        if args.capture_left is None or args.capture_top is None:
            parser.error("--capture-left and --capture-top are required for region capture mode")
    return args


def main(
    argv: Iterable[str] | None = None,
    *,
    stdout: TextIO | None = None,
    recognizer_factory=None,
    capture_thread_factory=None,
) -> int:
    args = parse_args(argv)
    stdout = stdout or sys.stdout
    recognizer_factory = recognizer_factory or _build_default_recognizer
    capture_thread_factory = capture_thread_factory or _build_capture_thread

    try:
        recognizer = recognizer_factory(args)
        capture_thread = capture_thread_factory(args)
        return _run_recognizer_loop(
            args=args,
            recognizer=recognizer,
            capture_thread=capture_thread,
            stdout=stdout,
        )
    except KeyboardInterrupt:
        return 0
    except (FileNotFoundError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(str(exc), file=sys.stderr)
        return 1


@dataclass(slots=True)
class SignatureWeaponRecognizer:
    game: str
    identity_records: tuple[WeaponIdentityRecord, ...]
    signature_records: tuple[VisualSignatureRecord, ...]
    profile_index: dict[str, tuple[str, ...]]
    ocr_reader: Any = None
    runtime_state: ResolverRuntimeState = field(default_factory=ResolverRuntimeState)
    adapter: Any = field(init=False)

    def __post_init__(self) -> None:
        self.adapter = get_adapter(self.game)

    def process_frame(self, frame: Any, *, frame_id: int, captured_at: float) -> RecognitionEvent | None:
        del frame_id
        del captured_at
        icon_region = _crop_normalized_roi(frame, self.adapter.weapon_icon_roi)
        if icon_region.size == 0:
            return None

        ranked_matches = score_candidates(_rgb_to_bgr(icon_region), self.signature_records)
        text_candidates = extract_text_candidates(
            frame,
            self.adapter.weapon_name_text_roi,
            ocr_reader=self.ocr_reader,
        )
        previous_weapon_id = self.runtime_state.confirmed_weapon_id
        switch_suspected = _should_suspect_switch(
            game=self.game,
            previous_weapon_id=previous_weapon_id,
            ranked_matches=ranked_matches,
        )
        result = resolve_weapon(
            adapter=self.adapter,
            identity_records=self.identity_records,
            ranked_image_matches=ranked_matches,
            text_candidates=text_candidates,
            runtime_state=self.runtime_state,
            switch_suspected=switch_suspected,
            timestamp=_utc_timestamp(),
        )
        self.runtime_state = result.runtime_state
        if _should_suppress_cod21_stale_carry_forward(
            game=self.game,
            previous_weapon_id=previous_weapon_id,
            ranked_matches=ranked_matches,
            event=result.event,
            switch_suspected=switch_suspected,
        ):
            return None
        return result.event

    def profile_ids_for(self, canonical_weapon_id: str) -> list[str]:
        return list(self.profile_index.get(canonical_weapon_id, ()))


def _run_recognizer_loop(*, args: argparse.Namespace, recognizer: Any, capture_thread: Any, stdout: TextIO) -> int:
    last_seen_id = 0
    last_emitted_key: str | None = None
    capture_thread.start()
    try:
        while True:
            captured_frame, last_seen_id = capture_thread.get_latest_frame(last_seen_id=last_seen_id, timeout=0.25)
            if captured_frame is None:
                if not getattr(capture_thread, "running", True):
                    break
                continue

            event = recognizer.process_frame(
                captured_frame.frame,
                frame_id=captured_frame.frame_id,
                captured_at=captured_frame.captured_at,
            )
            if event is None:
                continue

            payload = _build_event_payload(
                event,
                profile_ids=_lookup_profile_ids(recognizer, event.canonical_weapon_id),
            )
            payload_key = _payload_dedup_key(payload)
            if payload_key == last_emitted_key:
                continue

            stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
            stdout.flush()
            if args.latest_state_file is not None:
                _write_latest_state(args.latest_state_file, payload)
            last_emitted_key = payload_key
        return 0
    finally:
        capture_thread.stop()
        join = getattr(capture_thread, "join", None)
        if callable(join):
            join(timeout=1.0)


def _build_default_recognizer(args: argparse.Namespace) -> SignatureWeaponRecognizer:
    game = str(args.game)
    signature_records = _load_signature_records(args.signature_dir, game=game)
    identity_records = _load_identity_records_from_signature_dir(args.signature_dir, game=game)
    if not identity_records:
        # V1 keeps the fallback local and conservative: if explicit identity JSON
        # is absent, synthesize only the minimal records already named by the
        # signature payloads so the resolver can still emit canonical ids.
        identity_records = _infer_identity_records_from_signatures(signature_records)
    if not signature_records:
        raise FileNotFoundError(f"No visual signature records found in {args.signature_dir} for game {game!r}")
    profile_index = _load_profile_index(args.profile_dir, game=game)
    return SignatureWeaponRecognizer(
        game=game,
        identity_records=identity_records,
        signature_records=signature_records,
        profile_index=profile_index,
    )


def _build_capture_thread(args: argparse.Namespace) -> ScreenCaptureThread:
    if args.capture_mode == "fullscreen":
        screen_width = int(win32api.GetSystemMetrics(0))
        screen_height = int(win32api.GetSystemMetrics(1))
        return ScreenCaptureThread(
            target_fps=args.fps,
            region=(0, 0, screen_width, screen_height),
        )
    if args.capture_mode == "region":
        return ScreenCaptureThread(
            target_fps=args.fps,
            region=(
                int(args.capture_left),
                int(args.capture_top),
                int(args.capture_left + args.capture_width),
                int(args.capture_top + args.capture_height),
            ),
        )
    return ScreenCaptureThread(
        target_fps=args.fps,
        crop_width=int(args.capture_width),
        crop_height=int(args.capture_height),
    )


def _load_identity_records_from_signature_dir(directory: Path, *, game: str) -> tuple[WeaponIdentityRecord, ...]:
    records: list[WeaponIdentityRecord] = []
    for path, payload in _iter_json_payload_entries(directory):
        if not _looks_like_identity_payload(payload):
            continue
        try:
            record = WeaponIdentityRecord.from_dict(payload)
        except ValueError as exc:
            raise ValueError(f"Malformed weapon identity metadata in {path}: {exc}") from exc
        if record.game == game:
            records.append(record)
    return tuple(records)


def _load_signature_records(directory: Path, *, game: str) -> tuple[VisualSignatureRecord, ...]:
    records: list[VisualSignatureRecord] = []
    for _, payload in _iter_json_payload_entries(directory):
        try:
            record = VisualSignatureRecord.from_dict(payload)
        except ValueError:
            continue
        if record.game == game:
            records.append(record)
    return tuple(records)


def _load_profile_index(directory: Path, *, game: str) -> dict[str, tuple[str, ...]]:
    profile_ids_by_weapon: dict[str, list[str]] = {}
    for _, payload in _iter_json_payload_entries(directory):
        try:
            profile = RecoilProfileRecord.from_dict(payload)
        except ValueError:
            continue
        if profile.game != game:
            continue
        profile_ids_by_weapon.setdefault(profile.canonical_weapon_id, []).append(profile.profile_id)

    return {
        canonical_weapon_id: tuple(sorted(profile_ids))
        for canonical_weapon_id, profile_ids in profile_ids_by_weapon.items()
    }


def _iter_json_payload_entries(directory: Path) -> Iterable[tuple[Path, dict[str, Any]]]:
    if not directory.exists():
        return ()
    if not directory.is_dir():
        raise FileNotFoundError(f"Expected a directory path: {directory}")
    return tuple((path, _load_json(path)) for path in sorted(directory.glob("*.json")))


def _load_json(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise OSError(f"Unable to read JSON from {path}: {exc}") from exc
    except UnicodeDecodeError as exc:
        raise ValueError(f"Invalid UTF-8 in {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON in {path}: {exc.msg} (line {exc.lineno} column {exc.colno} char {exc.pos})") from exc

    if not isinstance(payload, dict):
        raise ValueError(f"JSON payload in {path} must be an object")
    return payload


def _infer_identity_records_from_signatures(
    signature_records: tuple[VisualSignatureRecord, ...],
) -> tuple[WeaponIdentityRecord, ...]:
    if not signature_records:
        return ()

    signature_ids_by_weapon: dict[str, list[str]] = {}
    for record in signature_records:
        signature_ids_by_weapon.setdefault(record.canonical_weapon_id, []).append(record.signature_id)

    inferred_records: list[WeaponIdentityRecord] = []
    for canonical_weapon_id, signature_ids in signature_ids_by_weapon.items():
        inferred_records.append(
            WeaponIdentityRecord(
                canonical_weapon_id=canonical_weapon_id,
                game=signature_records[0].game,
                weapon_family="unknown",
                display_name=canonical_weapon_id,
                alias_names=(),
                blueprint_names=(),
                signature_refs=tuple(sorted(signature_ids)),
                notes="inferred_from_signature_records",
                created_at="inferred",
                updated_at="inferred",
            )
        )
    return tuple(inferred_records)


def _should_suspect_switch(
    *,
    game: str,
    previous_weapon_id: str | None,
    ranked_matches: list[Any],
) -> bool:
    if game != "cod21" or previous_weapon_id is None:
        return False
    top_match = _top_ranked_match(ranked_matches)
    if top_match is None or top_match.canonical_weapon_id == previous_weapon_id:
        return False
    second_score = float(ranked_matches[1].score) if len(ranked_matches) > 1 else 0.0
    return bool(
        top_match.score >= _IMAGE_SWITCH_SCORE_THRESHOLD
        and (top_match.score - second_score) >= _IMAGE_SWITCH_MARGIN_THRESHOLD
    )


def _should_suppress_cod21_stale_carry_forward(
    *,
    game: str,
    previous_weapon_id: str | None,
    ranked_matches: list[Any],
    event: RecognitionEvent | None,
    switch_suspected: bool,
) -> bool:
    if (
        game != "cod21"
        or previous_weapon_id is None
        or switch_suspected
        or event is None
        or event.source != "carry_forward"
        or event.canonical_weapon_id != previous_weapon_id
    ):
        return False
    top_match = _top_ranked_match(ranked_matches)
    return top_match is not None and top_match.canonical_weapon_id != previous_weapon_id


def _top_ranked_match(ranked_matches: list[Any]) -> Any | None:
    if not ranked_matches:
        return None
    return ranked_matches[0]


def _looks_like_identity_payload(payload: dict[str, Any]) -> bool:
    return bool(_IDENTITY_HINT_KEYS.intersection(payload))


def _crop_normalized_roi(frame: Any, roi: NormalizedROI) -> Any:
    height, width = frame.shape[:2]
    left = max(0, min(width, int(round(roi.left * width))))
    top = max(0, min(height, int(round(roi.top * height))))
    right = max(left, min(width, int(round((roi.left + roi.width) * width))))
    bottom = max(top, min(height, int(round((roi.top + roi.height) * height))))
    return frame[top:bottom, left:right]


def _rgb_to_bgr(image: Any) -> Any:
    if getattr(image, "ndim", 0) == 3 and image.shape[2] >= 3:
        return image[:, :, :3][:, :, ::-1]
    return image


def _lookup_profile_ids(recognizer: Any, canonical_weapon_id: str) -> list[str]:
    profile_ids_for = getattr(recognizer, "profile_ids_for", None)
    if not callable(profile_ids_for):
        return []
    return list(profile_ids_for(canonical_weapon_id))


def _build_event_payload(event: RecognitionEvent, *, profile_ids: list[str]) -> dict[str, Any]:
    return {
        "type": "current_weapon",
        "game": event.game,
        "canonical_weapon_id": event.canonical_weapon_id,
        "confidence": event.confidence,
        "source": event.source,
        "timestamp": event.timestamp,
        "degraded": event.degraded,
        "matched_name": event.matched_name,
        "profile_ids": profile_ids,
    }


def _payload_dedup_key(payload: dict[str, Any]) -> str:
    dedup_payload = dict(payload)
    dedup_payload.pop("timestamp", None)
    return json.dumps(dedup_payload, ensure_ascii=False, sort_keys=True)


def _write_latest_state(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _utc_timestamp() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


if __name__ == "__main__":
    raise SystemExit(main())
