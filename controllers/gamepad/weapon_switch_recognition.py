from __future__ import annotations

from dataclasses import dataclass
from difflib import SequenceMatcher
import json
from pathlib import Path
import threading
import time
from typing import Any
from typing import Callable
from typing import Iterable

from runtime.recoil_sidecar.models import RecognizerState
from vision.recoil_collection.capture import build_full_screen_frame_grabber
from vision.weapon_identity.adapters import get_adapter
from vision.weapon_identity.models import WeaponIdentityRecord
from vision.weapon_identity.text import extract_text_candidates

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


@dataclass(slots=True, frozen=True)
class YButtonTextRecognitionConfig:
    sample_delays_ms: tuple[int, ...] = (150, 240, 340, 460)
    cache_publish_confidence: float = 0.90

    def __post_init__(self) -> None:
        if not self.sample_delays_ms:
            raise ValueError("YButtonTextRecognitionConfig.sample_delays_ms must not be empty")
        object.__setattr__(
            self,
            "sample_delays_ms",
            tuple(_require_non_negative_int(value, "YButtonTextRecognitionConfig.sample_delays_ms[]") for value in self.sample_delays_ms),
        )
        object.__setattr__(
            self,
            "cache_publish_confidence",
            _require_confidence(self.cache_publish_confidence, "YButtonTextRecognitionConfig.cache_publish_confidence"),
        )


class YButtonTextWeaponRecognizer:
    def __init__(
        self,
        *,
        game: str,
        identity_records: Iterable[WeaponIdentityRecord],
        state_writer: Callable[[RecognizerState], None],
        config: YButtonTextRecognitionConfig | None = None,
        frame_grabber_factory: Callable[[], Any] | None = None,
        ocr_reader: Any = None,
        sleep_fn: Callable[[float], None] | None = None,
        timestamp_fn: Callable[[], str] | None = None,
        capture_runner: Callable[[int, int, Callable[[], None]], None] | None = None,
    ) -> None:
        self.game = _require_non_empty_str(game, "game")
        self.adapter = get_adapter(self.game)
        self.identity_records = tuple(_require_identity_record(record, "identity_records[]") for record in identity_records)
        self._state_writer = state_writer
        self._config = config or YButtonTextRecognitionConfig()
        self._frame_grabber_factory = frame_grabber_factory or build_full_screen_frame_grabber
        self._ocr_reader = ocr_reader
        self._sleep_fn = sleep_fn or time.sleep
        self._timestamp_fn = timestamp_fn or _utc_timestamp
        self._capture_runner = capture_runner or _start_daemon_capture
        self._lock = threading.Lock()
        self._active_slot_index = 0
        self._switch_epoch = 0
        self._slot_states: list[RecognizerState | None] = [None, None]

    @classmethod
    def from_directory(
        cls,
        *,
        game: str,
        identity_dir: str | Path,
        state_path: str | Path,
        config: YButtonTextRecognitionConfig | None = None,
        frame_grabber_factory: Callable[[], Any] | None = None,
        ocr_reader: Any = None,
        sleep_fn: Callable[[float], None] | None = None,
        timestamp_fn: Callable[[], str] | None = None,
        capture_runner: Callable[[int, int, Callable[[], None]], None] | None = None,
    ) -> "YButtonTextWeaponRecognizer":
        identity_path = Path(identity_dir)
        state_file = Path(state_path)
        identity_records = _load_identity_records(identity_path, game=game)
        return cls(
            game=game,
            identity_records=identity_records,
            state_writer=lambda state: _write_state_file(state_file, state),
            config=config,
            frame_grabber_factory=frame_grabber_factory,
            ocr_reader=ocr_reader,
            sleep_fn=sleep_fn,
            timestamp_fn=timestamp_fn,
            capture_runner=capture_runner,
        )

    @property
    def active_slot_index(self) -> int:
        with self._lock:
            return self._active_slot_index

    def handle_switch_pressed(self) -> None:
        with self._lock:
            self._active_slot_index = 1 - self._active_slot_index
            self._switch_epoch += 1
            slot_index = self._active_slot_index
            switch_epoch = self._switch_epoch
            cached_state = self._slot_states[slot_index]

        if cached_state is not None:
            self._state_writer(_clone_state(cached_state, source="switch_cache", timestamp=self._timestamp_fn()))

        self._capture_runner(
            slot_index,
            switch_epoch,
            lambda: self._run_capture(slot_index=slot_index, switch_epoch=switch_epoch),
        )

    def complete_switch_resolution(
        self,
        *,
        slot_index: int,
        switch_epoch: int,
        state: RecognizerState | None,
    ) -> None:
        if state is None:
            return
        normalized_slot = _require_slot_index(slot_index, "slot_index")
        normalized_epoch = _require_non_negative_int(switch_epoch, "switch_epoch")
        _require_recognizer_state(state, "state")

        with self._lock:
            self._slot_states[normalized_slot] = state
            should_publish = (
                normalized_slot == self._active_slot_index
                and normalized_epoch == self._switch_epoch
            )
        if should_publish:
            self._state_writer(_clone_state(state, source="switch_text", timestamp=state.timestamp))

    def _run_capture(self, *, slot_index: int, switch_epoch: int) -> None:
        state = self._capture_best_state()
        self.complete_switch_resolution(
            slot_index=slot_index,
            switch_epoch=switch_epoch,
            state=state,
        )

    def _capture_best_state(self) -> RecognizerState | None:
        votes: list[tuple[str, str]] = []
        frame_grabber = self._frame_grabber_factory()
        started_at = time.perf_counter()
        try:
            for delay_ms in self._config.sample_delays_ms:
                remaining = (float(delay_ms) / 1000.0) - (time.perf_counter() - started_at)
                if remaining > 0.0:
                    self._sleep_fn(remaining)
                frame = frame_grabber.grab()
                text_candidates = extract_text_candidates(
                    frame,
                    self.adapter.weapon_name_text_roi,
                    ocr_reader=self._ocr_reader,
                    multi_pass=True,
                )
                for candidate_name in text_candidates:
                    canonical_weapon_id = _resolve_name(candidate_name, self.identity_records)
                    if canonical_weapon_id is None:
                        continue
                    votes.append((canonical_weapon_id, candidate_name))
        finally:
            close = getattr(frame_grabber, "close", None)
            if callable(close):
                close()

        resolved_vote = _select_best_vote(votes)
        if resolved_vote is None:
            return None

        canonical_weapon_id, matched_name, confidence = resolved_vote
        return RecognizerState(
            game=self.game,
            canonical_weapon_id=canonical_weapon_id,
            confidence=confidence,
            source="switch_text",
            timestamp=self._timestamp_fn(),
            degraded=False,
            matched_name=matched_name,
            profile_ids=(),
        )


def _load_identity_records(directory: Path, *, game: str) -> tuple[WeaponIdentityRecord, ...]:
    if not directory.exists() or not directory.is_dir():
        return ()

    records: list[WeaponIdentityRecord] = []
    for path in sorted(directory.glob("*.json")):
        payload = _load_json(path)
        if not _looks_like_identity_payload(payload):
            continue
        record = WeaponIdentityRecord.from_dict(payload)
        if record.game == game:
            records.append(record)
    return tuple(records)


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


def _looks_like_identity_payload(payload: dict[str, Any]) -> bool:
    return bool(_IDENTITY_HINT_KEYS.intersection(payload))


def _resolve_name(candidate_name: str, identity_records: tuple[WeaponIdentityRecord, ...]) -> str | None:
    normalized_candidate = _normalize_name(candidate_name)
    if normalized_candidate is None:
        return None

    for record in identity_records:
        canonical_weapon_id = record.resolve_name(candidate_name)
        if canonical_weapon_id is not None:
            return canonical_weapon_id

    scored_matches: list[tuple[float, str]] = []
    for record in identity_records:
        score = _score_identity_match(normalized_candidate, record)
        if score > 0.0:
            scored_matches.append((score, record.canonical_weapon_id))

    if not scored_matches:
        return None

    scored_matches.sort(reverse=True)
    top_score, top_weapon_id = scored_matches[0]
    second_score = scored_matches[1][0] if len(scored_matches) > 1 else 0.0
    if top_score >= 0.78 and (top_score - second_score) >= 0.08:
        return top_weapon_id
    return None


def _score_identity_match(normalized_candidate: str, record: WeaponIdentityRecord) -> float:
    best_score = 0.0
    for candidate_name in _iter_identity_names(record):
        normalized_name = _normalize_name(candidate_name)
        if normalized_name is None:
            continue
        score = SequenceMatcher(None, normalized_candidate, normalized_name).ratio()
        if normalized_candidate in normalized_name or normalized_name in normalized_candidate:
            score += 0.25
        best_score = max(best_score, score)
    return min(1.0, best_score)


def _iter_identity_names(record: WeaponIdentityRecord) -> tuple[str, ...]:
    return (
        record.canonical_weapon_id,
        record.display_name,
        *record.alias_names,
        *record.blueprint_names,
    )


def _select_best_vote(votes: list[tuple[str, str]]) -> tuple[str, str, float] | None:
    if not votes:
        return None

    counts: dict[str, int] = {}
    first_index: dict[str, int] = {}
    matched_name_by_id: dict[str, str] = {}
    for index, (canonical_weapon_id, matched_name) in enumerate(votes):
        counts[canonical_weapon_id] = counts.get(canonical_weapon_id, 0) + 1
        first_index.setdefault(canonical_weapon_id, index)
        if canonical_weapon_id not in matched_name_by_id or len(matched_name) > len(matched_name_by_id[canonical_weapon_id]):
            matched_name_by_id[canonical_weapon_id] = matched_name

    best_weapon_id = max(
        counts,
        key=lambda canonical_weapon_id: (
            counts[canonical_weapon_id],
            len(matched_name_by_id[canonical_weapon_id]),
            -first_index[canonical_weapon_id],
        ),
    )
    vote_count = counts[best_weapon_id]
    confidence = min(0.98, 0.78 + (0.06 * vote_count))
    return best_weapon_id, matched_name_by_id[best_weapon_id], confidence


def _write_state_file(path: Path, state: RecognizerState) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state.to_dict(), ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _clone_state(state: RecognizerState, *, source: str, timestamp: str) -> RecognizerState:
    return RecognizerState(
        game=state.game,
        canonical_weapon_id=state.canonical_weapon_id,
        confidence=state.confidence,
        source=source,
        timestamp=timestamp,
        degraded=False,
        matched_name=state.matched_name,
        profile_ids=state.profile_ids,
    )


def _start_daemon_capture(slot_index: int, switch_epoch: int, task: Callable[[], None]) -> None:
    del slot_index
    del switch_epoch
    thread = threading.Thread(target=task, daemon=True)
    thread.start()


def _utc_timestamp() -> str:
    from datetime import datetime
    from datetime import timezone

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _require_identity_record(value: Any, label: str) -> WeaponIdentityRecord:
    if not isinstance(value, WeaponIdentityRecord):
        raise ValueError(f"{label} must contain WeaponIdentityRecord values")
    return value


def _require_recognizer_state(value: Any, label: str) -> RecognizerState:
    if not isinstance(value, RecognizerState):
        raise ValueError(f"{label} must be a RecognizerState")
    return value


def _require_confidence(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be a number")
    confidence = float(value)
    if confidence < 0.0 or confidence > 1.0:
        raise ValueError(f"{label} must be between 0.0 and 1.0")
    return confidence


def _require_non_negative_int(value: Any, label: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{label} must be an integer")
    if value < 0:
        raise ValueError(f"{label} must be greater than or equal to zero")
    return value


def _require_slot_index(value: Any, label: str) -> int:
    slot_index = _require_non_negative_int(value, label)
    if slot_index not in {0, 1}:
        raise ValueError(f"{label} must be 0 or 1")
    return slot_index


def _require_non_empty_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    result = value.strip()
    if not result:
        raise ValueError(f"{label} must be a non-empty string")
    return result


def _normalize_name(value: Any) -> str | None:
    if type(value) is not str:
        return None
    return "".join(value.strip().split()).casefold() or None


__all__ = [
    "YButtonTextRecognitionConfig",
    "YButtonTextWeaponRecognizer",
]
