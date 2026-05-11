from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import threading
import time
from typing import Any
from typing import Callable
from typing import Iterable
from typing import TextIO

from runtime.recoil_sidecar.models import RecognizerState
from vision.recoil_collection.capture import RecoilCollectorConfig
from vision.recoil_collection.capture import RecoilCollectionError
from vision.recoil_collection.capture import _collect_motion_trace_from_thread
from vision.recoil_collection.capture import collect_recoil_profile
from vision.weapon_identity.adapters import NormalizedROI
from vision.recoil_collection.storage import load_recoil_profile
from vision.recoil_collection.storage import save_recoil_profile
from vision.recoil_collection.storage import save_recoil_profile_summary
from vision.weapon_identity.adapters import get_adapter
from vision.weapon_identity.models import RecognitionEvent
from vision.weapon_identity.text import extract_text_candidates
from vision.weapon_identity.text import normalize_ocr_lines


@dataclass(slots=True, frozen=True)
class RecoilAppConfig:
    game: str
    mode: str = "record"
    weapon_dir: str = "artifacts/recoil_app/weapons"
    profile_dir: str = "artifacts/recoil_profiles"
    state_path: str = "artifacts/recoil_app/current_weapon.json"
    plot_dir: str = "artifacts/recoil_plots"
    startup_delay_ms: tuple[int, ...] = (600, 760)

    def __post_init__(self) -> None:
        object.__setattr__(self, "game", _require_non_empty_str(self.game, "RecoilAppConfig.game"))
        mode = _require_non_empty_str(self.mode, "RecoilAppConfig.mode").casefold()
        if mode not in {"record", "recoil"}:
            raise ValueError("RecoilAppConfig.mode must be one of ['record', 'recoil']")
        object.__setattr__(self, "mode", mode)
        object.__setattr__(self, "weapon_dir", _require_non_empty_str(self.weapon_dir, "RecoilAppConfig.weapon_dir"))
        object.__setattr__(self, "profile_dir", _require_non_empty_str(self.profile_dir, "RecoilAppConfig.profile_dir"))
        object.__setattr__(self, "state_path", _require_non_empty_str(self.state_path, "RecoilAppConfig.state_path"))
        object.__setattr__(self, "plot_dir", _require_non_empty_str(self.plot_dir, "RecoilAppConfig.plot_dir"))
        delays = tuple(_require_non_negative_int(value, "RecoilAppConfig.startup_delay_ms[]") for value in self.startup_delay_ms)
        if not delays:
            raise ValueError("RecoilAppConfig.startup_delay_ms must not be empty")
        object.__setattr__(self, "startup_delay_ms", _normalize_switch_capture_delays(delays))


@dataclass(slots=True, frozen=True)
class RecoilWeaponRecord:
    canonical_weapon_id: str
    game: str
    display_name: str
    created_at: str
    updated_at: str

    def __post_init__(self) -> None:
        object.__setattr__(self, "canonical_weapon_id", _require_non_empty_str(self.canonical_weapon_id, "RecoilWeaponRecord.canonical_weapon_id"))
        object.__setattr__(self, "game", _require_non_empty_str(self.game, "RecoilWeaponRecord.game"))
        object.__setattr__(self, "display_name", _require_non_empty_str(self.display_name, "RecoilWeaponRecord.display_name"))
        object.__setattr__(self, "created_at", _require_non_empty_str(self.created_at, "RecoilWeaponRecord.created_at"))
        object.__setattr__(self, "updated_at", _require_non_empty_str(self.updated_at, "RecoilWeaponRecord.updated_at"))

    def to_dict(self) -> dict[str, str]:
        return {
            "canonical_weapon_id": self.canonical_weapon_id,
            "game": self.game,
            "display_name": self.display_name,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RecoilWeaponRecord":
        if set(data) != {"canonical_weapon_id", "game", "display_name", "created_at", "updated_at"}:
            raise ValueError("RecoilWeaponRecord payload must contain only canonical_weapon_id, game, display_name, created_at, updated_at")
        return cls(
            canonical_weapon_id=data["canonical_weapon_id"],
            game=data["game"],
            display_name=data["display_name"],
            created_at=data["created_at"],
            updated_at=data["updated_at"],
        )


class IdentityStore:
    def __init__(self, directory: Path):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._records_by_game: dict[str, tuple[RecoilWeaponRecord, ...]] = {}
        self._records_by_key: dict[tuple[str, str], RecoilWeaponRecord] = {}
        self._load_records()

    def records_for_game(self, game: str) -> tuple[RecoilWeaponRecord, ...]:
        normalized_game = _require_non_empty_str(game, "game")
        with self._lock:
            return self._records_by_game.get(normalized_game, ())

    def resolve_or_create(self, *, game: str, display_name: str, timestamp: str) -> RecoilWeaponRecord:
        normalized_game = _require_non_empty_str(game, "game")
        normalized_name = _require_non_empty_str(display_name, "display_name")
        canonical_weapon_id = _build_canonical_weapon_id(normalized_game, normalized_name)
        with self._lock:
            for record in self._records_by_game.get(normalized_game, ()):
                if record.canonical_weapon_id == canonical_weapon_id or record.display_name == normalized_name:
                    return record
            record = RecoilWeaponRecord(
                canonical_weapon_id=canonical_weapon_id,
                game=normalized_game,
                display_name=normalized_name,
                created_at=timestamp,
                updated_at=timestamp,
            )
            self._save_record(record)
            records = [*self._records_by_game.get(normalized_game, ()), record]
            self._records_by_game[normalized_game] = tuple(records)
            self._records_by_key[(normalized_game, canonical_weapon_id)] = record
            return record

    def _load_records(self) -> None:
        records_by_game: dict[str, list[RecoilWeaponRecord]] = {}
        records_by_key: dict[tuple[str, str], RecoilWeaponRecord] = {}
        for path in sorted(self.directory.glob("identity-*.json")):
            try:
                payload = json.loads(path.read_text(encoding="utf-8"))
                if not isinstance(payload, dict):
                    continue
                record = RecoilWeaponRecord.from_dict(payload)
            except Exception:
                continue
            records_by_game.setdefault(record.game, []).append(record)
            records_by_key[(record.game, record.canonical_weapon_id)] = record
        self._records_by_game = {game: tuple(records) for game, records in records_by_game.items()}
        self._records_by_key = records_by_key

    def _save_record(self, record: RecoilWeaponRecord) -> None:
        path = self.directory / f"identity-{record.game}-{record.canonical_weapon_id}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(record.to_dict(), ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


class RecoilProfileStore:
    def __init__(self, directory: Path):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._records: tuple[Any, ...] = ()
        self._records_by_key: dict[tuple[str, str, str, str], tuple[Any, ...]] = {}
        self._load_generation = 0
        self.reload()

    @property
    def load_generation(self) -> int:
        return self._load_generation

    def upsert(self, profile_record) -> None:
        with self._lock:
            save_recoil_profile(self.directory / f"{profile_record.profile_id}.json", profile_record)
            records = [record for record in self._records if record.profile_id != profile_record.profile_id]
            records.append(profile_record)
            self._set_records(tuple(records))
            self._load_generation += 1

    def get_best_profile(self, *, game: str, canonical_weapon_id: str, stance: str, aim_mode: str):
        key = (game, canonical_weapon_id, stance, aim_mode)
        matches = self._records_by_key.get(key, ())
        if not matches:
            return None
        return matches[0]

    def profile_ids_for_weapon(self, *, game: str, canonical_weapon_id: str, stance: str = "standing") -> tuple[str, ...]:
        profile_ids: list[str] = []
        for aim_mode in ("ads", "hipfire"):
            matches = self._records_by_key.get((game, canonical_weapon_id, stance, aim_mode), ())
            profile_ids.extend(record.profile_id for record in matches)
        return tuple(profile_ids)

    def reload(self) -> None:
        with self._lock:
            records = []
            for path in sorted(self.directory.glob("*.json")):
                if path.name.endswith(".summary.json"):
                    continue
                try:
                    records.append(load_recoil_profile(path))
                except Exception:
                    continue
            self._set_records(tuple(records))
            self._load_generation += 1

    def _set_records(self, records: tuple[Any, ...]):
        self._records = tuple(records)
        by_key: dict[tuple[str, str, str, str], list[Any]] = {}
        for record in self._records:
            key = (record.game, record.canonical_weapon_id, record.stance, record.aim_mode)
            by_key.setdefault(key, []).append(record)
        self._records_by_key = {
            key: tuple(sorted(values, key=lambda item: (-item.confidence, item.profile_id)))
            for key, values in by_key.items()
        }


class RecoilRuntime:
    def __init__(
        self,
        *,
        game: str,
        identity_store: IdentityStore,
        profile_store: RecoilProfileStore,
        mode: str = "record",
        state_path: Path | None = None,
        plot_dir: Path | None = None,
        frame_grabber_factory: Callable[[], Any] | None = None,
        ocr_reader: Any = None,
        sleep_fn: Callable[[float], None] | None = None,
        timestamp_fn: Callable[[], str] | None = None,
        switch_task_runner: Callable[[int, int, Callable[[], None]], None] | None = None,
        learning_task_runner: Callable[[str, str, Callable[[], None]], None] | None = None,
        collector_config: RecoilCollectorConfig | None = None,
        switch_capture_delays_ms: tuple[int, ...] | None = None,
        motion_frame_grabber_factory: Callable[[], Any] | None = None,
        stdout: TextIO | None = None,
    ) -> None:
        self.game = _require_non_empty_str(game, "game")
        self.mode = _require_non_empty_str(mode, "mode").casefold()
        if self.mode not in {"record", "recoil"}:
            raise ValueError("mode must be one of ['record', 'recoil']")
        self.identity_store = identity_store
        self.profile_store = profile_store
        self.state_path = Path(state_path) if state_path is not None else None
        self.plot_dir = Path(plot_dir) if plot_dir is not None else None
        if self.plot_dir is not None:
            self.plot_dir.mkdir(parents=True, exist_ok=True)
        self.adapter = get_adapter(self.game)
        self._frame_grabber_factory = frame_grabber_factory or self._build_default_frame_grabber
        self._ocr_reader = ocr_reader
        self._sleep_fn = sleep_fn or time.sleep
        self._timestamp_fn = timestamp_fn or _utc_timestamp
        self._switch_task_runner = switch_task_runner or self._start_switch_task
        self._learning_task_runner = learning_task_runner or self._start_learning_task
        if collector_config is not None:
            self._collector_config = collector_config
        elif self.mode == "record":
            self._collector_config = RecoilCollectorConfig(
                capture_fps=60,
                min_clean_bursts=1,
                target_clean_bursts=4,
            )
        else:
            self._collector_config = RecoilCollectorConfig()
        delays = tuple(int(value) for value in (switch_capture_delays_ms or (600, 760)))
        self._switch_capture_delays_ms = tuple(delay for delay in delays if delay >= 0) or (600, 760)
        self._motion_frame_grabber_factory = motion_frame_grabber_factory or self._build_default_motion_frame_grabber
        self._stdout = stdout or _NullTextIO()
        self._lock = threading.Lock()
        self._active_slot_index = 0
        self._switch_epoch = 0
        self._slot_states: list[RecognizerState | None] = [None, None]
        self._current_firing = False
        self._learning_keys: set[tuple[str, str]] = set()
        self._switch_frame_grabber: Any | None = None
        self._switch_frame_grabber_lock = threading.Lock()
        self._switch_capture_roi = _build_switch_capture_roi(self.game)

    @property
    def current_state(self) -> RecognizerState | None:
        with self._lock:
            return self._slot_states[self._active_slot_index]

    def handle_switch_pressed(self) -> None:
        with self._lock:
            self._active_slot_index = 1 - self._active_slot_index
            self._switch_epoch += 1
            slot_index = self._active_slot_index
            switch_epoch = self._switch_epoch
            cached_state = self._slot_states[slot_index]

        if cached_state is not None:
            self._publish_state(cached_state, source="switch_cache")
            return

        self._switch_task_runner(
            slot_index,
            switch_epoch,
            lambda: self._run_switch_capture(slot_index=slot_index, switch_epoch=switch_epoch),
        )

    def complete_switch_resolution(self, *, slot_index: int, switch_epoch: int, state: RecognizerState | None) -> None:
        if state is None:
            return
        normalized_slot = _require_slot_index(slot_index, "slot_index")
        normalized_epoch = _require_non_negative_int(switch_epoch, "switch_epoch")
        with self._lock:
            self._slot_states[normalized_slot] = state
            should_publish = normalized_slot == self._active_slot_index and normalized_epoch == self._switch_epoch
        if should_publish:
            self._publish_state(state, source=state.source)

    def handle_fire_state(self, *, is_firing: bool, aim_mode: str) -> None:
        normalized_aim_mode = _require_non_empty_str(aim_mode, "aim_mode")
        if normalized_aim_mode not in {"ads", "hipfire"}:
            raise ValueError("aim_mode must be one of ['ads', 'hipfire']")
        previous = self._current_firing
        self._current_firing = bool(is_firing)
        if self.mode != "record":
            return
        if not self._current_firing or previous:
            return
        current_state = self.current_state
        if current_state is None:
            return
        if self.profile_store.get_best_profile(
            game=current_state.game,
            canonical_weapon_id=current_state.canonical_weapon_id,
            stance="standing",
            aim_mode=normalized_aim_mode,
        ) is not None:
            return
        learning_key = (current_state.canonical_weapon_id, normalized_aim_mode)
        with self._lock:
            if learning_key in self._learning_keys:
                return
            self._learning_keys.add(learning_key)
        self._learning_task_runner(
            current_state.canonical_weapon_id,
            normalized_aim_mode,
            lambda: self._run_learning_capture(current_state=current_state, aim_mode=normalized_aim_mode),
        )

    def get_active_profile(self, *, aim_mode: str, stance: str = "standing"):
        if self.mode != "recoil":
            return None
        current_state = self.current_state
        if current_state is None:
            return None
        return self.profile_store.get_best_profile(
            game=current_state.game,
            canonical_weapon_id=current_state.canonical_weapon_id,
            stance=stance,
            aim_mode=aim_mode,
        )

    def _run_switch_capture(self, *, slot_index: int, switch_epoch: int) -> None:
        try:
            started_at = time.perf_counter()
            capture_delays = self._resolve_switch_capture_delays_ms()
            for index, delay_ms in enumerate(capture_delays):
                if self._is_stale_switch_epoch(slot_index=slot_index, switch_epoch=switch_epoch):
                    return
                remaining = (float(delay_ms) / 1000.0) - (time.perf_counter() - started_at)
                if remaining > 0.0:
                    self._sleep_fn(remaining)
                frame = self._grab_switch_frame()
                text_candidates = extract_text_candidates(
                    frame,
                    self._switch_capture_roi,
                    ocr_reader=self._ocr_reader,
                    multi_pass=False,
                )
                matched_name = _select_best_name(list(text_candidates))
                if matched_name is None and index == len(capture_delays) - 1:
                    text_candidates = extract_text_candidates(
                        frame,
                        self._switch_capture_roi,
                        ocr_reader=self._ocr_reader,
                        multi_pass=True,
                    )
                    matched_name = _select_best_name(list(text_candidates))
                if matched_name is not None:
                    self._complete_switch_name_match(
                        slot_index=slot_index,
                        switch_epoch=switch_epoch,
                        matched_name=matched_name,
                    )
                    return
            self._log_switch_capture_error("no_valid_name")
        except Exception as exc:
            self._reset_switch_frame_grabber()
            self._log_switch_capture_error(str(exc))

    def _run_learning_capture(self, *, current_state: RecognizerState, aim_mode: str) -> None:
        learning_key = (current_state.canonical_weapon_id, aim_mode)
        try:
            motion_sampler = self._build_runtime_motion_sampler()
            result = collect_recoil_profile(
                game=self.game,
                aim_mode=aim_mode,
                standing_only=True,
                recognizer=None,
                weapon_frame_source=None,
                motion_sampler=motion_sampler,
                recognition_event_override=RecognitionEvent(
                    game=current_state.game,
                    canonical_weapon_id=current_state.canonical_weapon_id,
                    confidence=current_state.confidence,
                    source=current_state.source,
                    timestamp=current_state.timestamp,
                    degraded=False,
                    matched_name=current_state.matched_name,
                ),
                config=self._collector_config,
                timestamp_fn=self._timestamp_fn,
            )
            profile_path = self.profile_store.directory / f"{result.extracted_profile.profile.profile_id}.json"
            summary_path = self.profile_store.directory / f"{result.profile_summary.profile_id}.summary.json"
            save_recoil_profile(profile_path, result.extracted_profile.profile)
            save_recoil_profile_summary(summary_path, result.profile_summary)
            self.profile_store.upsert(result.extracted_profile.profile)
            if self.plot_dir is not None:
                _write_plot(
                    self.plot_dir / f"{result.extracted_profile.profile.profile_id}.png",
                    result.burst_series,
                    result.extracted_profile.profile,
                )
            self._publish_state(current_state, source="learned")
        except RecoilCollectionError as exc:
            self._stdout.write(
                f"[Recoil] learn_skip weapon={current_state.matched_name or current_state.canonical_weapon_id} "
                f"aim={aim_mode} reason={exc}\n"
            )
            self._stdout.flush()
            return
        except Exception as exc:
            self._stdout.write(
                f"[Recoil] learn_error weapon={current_state.matched_name or current_state.canonical_weapon_id} "
                f"aim={aim_mode} error={exc}\n"
            )
            self._stdout.flush()
            return
        finally:
            with self._lock:
                self._learning_keys.discard(learning_key)

    def _build_runtime_motion_sampler(self) -> Callable[[], Iterable[Any]]:
        def _sample():
            capture_thread = _PollingFrameCaptureSource(
                frame_grabber=self._motion_frame_grabber_factory(),
                target_fps=self._collector_config.capture_fps,
            )
            try:
                return _collect_motion_trace_from_thread(
                    capture_thread=capture_thread,
                    config=self._collector_config,
                    fire_input_source=_RuntimeFireInputSource(self),
                )
            finally:
                capture_thread.close()

        return _sample

    def _publish_state(self, state: RecognizerState, *, source: str) -> None:
        profile_ads = self.profile_store.get_best_profile(
            game=state.game,
            canonical_weapon_id=state.canonical_weapon_id,
            stance="standing",
            aim_mode="ads",
        )
        profile_hip = self.profile_store.get_best_profile(
            game=state.game,
            canonical_weapon_id=state.canonical_weapon_id,
            stance="standing",
            aim_mode="hipfire",
        )
        compensation_enabled = self.mode == "recoil"
        fallback_active = True if not compensation_enabled else profile_ads is None and profile_hip is None
        if compensation_enabled:
            status = f"fallback={'20%' if fallback_active else 'profile'}"
        else:
            status = "compensation=off(record)"
        self._stdout.write(
            f"[Recoil] slot={self._active_slot_index} weapon={state.matched_name or state.canonical_weapon_id} "
            f"source={source} {status}\n"
        )
        self._stdout.flush()
        if self.state_path is not None:
            payload = state.to_dict()
            payload.update(
                {
                    "active_slot_index": self._active_slot_index,
                    "fallback_active": fallback_active,
                    "active_profile_ids": list(state.profile_ids),
                    "mode": self.mode,
                    "compensation_enabled": compensation_enabled,
                }
            )
            self.state_path.parent.mkdir(parents=True, exist_ok=True)
            self.state_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def close(self) -> None:
        self._reset_switch_frame_grabber()

    def _build_default_frame_grabber(self):
        bbox = _build_primary_display_roi_bbox(self.adapter.weapon_name_text_roi)
        try:
            from vision.dxgi_capture import create_capture_backend

            backend = create_capture_backend(region=bbox, output_color="RGB")
            return _BackendSwitchFrameGrabber(backend=backend)
        except Exception:
            from PIL import ImageGrab

        return _ImageGrabFrameGrabber(image_grab_module=ImageGrab, bbox=bbox, all_screens=False)

    def _build_default_motion_frame_grabber(self):
        bbox = _build_center_region_bbox(
            width=self._collector_config.capture_width,
            height=self._collector_config.capture_height,
        )
        try:
            from vision.dxgi_capture import create_capture_backend

            backend = create_capture_backend(region=bbox, output_color="RGB")
            return _BackendSwitchFrameGrabber(backend=backend)
        except Exception:
            from PIL import ImageGrab

        return _ImageGrabFrameGrabber(image_grab_module=ImageGrab, bbox=bbox, all_screens=False)

    @staticmethod
    def _start_switch_task(slot_index: int, switch_epoch: int, task: Callable[[], None]) -> None:
        del slot_index
        del switch_epoch
        threading.Thread(target=task, daemon=True).start()

    @staticmethod
    def _start_learning_task(canonical_weapon_id: str, aim_mode: str, task: Callable[[], None]) -> None:
        del canonical_weapon_id
        del aim_mode
        threading.Thread(target=task, daemon=True).start()

    def _get_or_create_switch_frame_grabber(self):
        with self._switch_frame_grabber_lock:
            if self._switch_frame_grabber is None:
                self._switch_frame_grabber = self._frame_grabber_factory()
            return self._switch_frame_grabber

    def _grab_switch_frame(self):
        with self._switch_frame_grabber_lock:
            if self._switch_frame_grabber is None:
                self._switch_frame_grabber = self._frame_grabber_factory()
            return self._switch_frame_grabber.grab()

    def _reset_switch_frame_grabber(self) -> None:
        with self._switch_frame_grabber_lock:
            frame_grabber = self._switch_frame_grabber
            self._switch_frame_grabber = None
        if frame_grabber is None:
            return
        close = getattr(frame_grabber, "close", None)
        if callable(close):
            try:
                close()
            except Exception:
                return

    def _is_stale_switch_epoch(self, *, slot_index: int, switch_epoch: int) -> bool:
        with self._lock:
            return slot_index != self._active_slot_index or switch_epoch != self._switch_epoch

    def _log_switch_capture_error(self, reason: str) -> None:
        self._stdout.write(f"[Recoil] switch_capture_error game={self.game} reason={reason}\n")
        self._stdout.flush()

    def _complete_switch_name_match(self, *, slot_index: int, switch_epoch: int, matched_name: str) -> None:
        timestamp = self._timestamp_fn()
        identity = self.identity_store.resolve_or_create(game=self.game, display_name=matched_name, timestamp=timestamp)
        profile_ids = self.profile_store.profile_ids_for_weapon(
            game=self.game,
            canonical_weapon_id=identity.canonical_weapon_id,
            stance="standing",
        )
        state = RecognizerState(
            game=self.game,
            canonical_weapon_id=identity.canonical_weapon_id,
            confidence=0.92,
            source="switch_text",
            timestamp=timestamp,
            degraded=False,
            matched_name=identity.display_name,
            profile_ids=profile_ids,
        )
        self.complete_switch_resolution(slot_index=slot_index, switch_epoch=switch_epoch, state=state)

    def _resolve_switch_capture_delays_ms(self) -> tuple[int, ...]:
        return _normalize_switch_capture_delays(self._switch_capture_delays_ms)


class GamepadRecoilBridge:
    def __init__(self, runtime: RecoilRuntime):
        self.runtime = runtime
        self._last_y_pressed = False

    @classmethod
    def from_env(cls, *, base_dir: Path | None = None) -> "GamepadRecoilBridge" | None:
        import os

        enabled = os.environ.get("ENABLE_RECOIL_APP", "").strip().casefold()
        if enabled not in {"1", "true", "yes", "on"}:
            return None
        game = os.environ.get("RECOIL_GAME", "").strip()
        if not game:
            return None
        root = Path(base_dir or Path.cwd())
        defaults = RecoilAppConfig(
            game=game,
            mode=os.environ.get("RECOIL_APP_MODE", "recoil"),
            weapon_dir=os.environ.get(
                "RECOIL_WEAPON_DIR",
                os.environ.get("RECOIL_SIGNATURE_DIR", str(root / "artifacts" / "recoil_app" / "weapons")),
            ),
            profile_dir=os.environ.get("RECOIL_PROFILE_DIR", str(root / "artifacts" / "recoil_profiles")),
            state_path=os.environ.get(
                "RECOIL_STATE_PATH",
                str(Path(os.environ.get("RECOIL_STATE_DIR", str(root / "artifacts" / "recoil_app"))) / "current_weapon.json"),
            ),
            plot_dir=os.environ.get("RECOIL_PLOT_DIR", str(root / "artifacts" / "recoil_plots")),
        )
        runtime = RecoilRuntime(
            game=defaults.game,
            mode=defaults.mode,
            identity_store=IdentityStore(Path(defaults.weapon_dir)),
            profile_store=RecoilProfileStore(Path(defaults.profile_dir)),
            state_path=Path(defaults.state_path),
            plot_dir=Path(defaults.plot_dir),
            switch_capture_delays_ms=defaults.startup_delay_ms,
            stdout=_StdoutProxy(),
        )
        return cls(runtime)

    def handle_buttons(self, buttons: dict[str, bool]) -> None:
        current_y = bool(buttons.get("y", False))
        if current_y and not self._last_y_pressed:
            self.runtime.handle_switch_pressed()
        self._last_y_pressed = current_y

    def handle_fire_state(self, *, is_firing: bool, is_aiming: bool) -> None:
        self.runtime.handle_fire_state(
            is_firing=bool(is_firing),
            aim_mode="ads" if is_aiming else "hipfire",
        )

    def get_active_profile(self, *, is_aiming: bool):
        return self.runtime.get_active_profile(aim_mode="ads" if is_aiming else "hipfire")


class _RuntimeFireInputSource:
    def __init__(self, runtime: RecoilRuntime):
        self._runtime = runtime

    def is_firing(self) -> bool:
        return bool(self._runtime._current_firing)


class _StdoutProxy:
    def write(self, text: str) -> int:
        import sys

        return sys.stdout.write(text)

    def flush(self) -> None:
        import sys

        sys.stdout.flush()


class _NullTextIO:
    def write(self, text: str) -> int:
        return len(text)

    def flush(self) -> None:
        return None


class _ImageGrabFrameGrabber:
    def __init__(self, *, image_grab_module: Any, bbox: tuple[int, int, int, int], all_screens: bool):
        self._image_grab_module = image_grab_module
        self._bbox = bbox
        self._all_screens = all_screens

    def grab(self):
        import numpy as np

        frame = self._image_grab_module.grab(bbox=self._bbox, all_screens=self._all_screens)
        if frame is None:
            raise ValueError("Unable to capture a HUD frame for weapon confirmation")
        if hasattr(frame, "convert"):
            frame = frame.convert("RGB")
        return np.asarray(frame)

    def close(self) -> None:
        return None


class _BackendSwitchFrameGrabber:
    def __init__(self, *, backend: Any):
        self._backend = backend

    def grab(self):
        frame = self._backend.grab()
        if frame is None:
            raise ValueError("Unable to capture a HUD frame for weapon confirmation")
        return frame

    def close(self) -> None:
        self._backend.close()


class _PollingFrameCaptureSource:
    def __init__(self, *, frame_grabber: Any, target_fps: int):
        self._frame_grabber = frame_grabber
        self._target_fps = max(1.0, float(target_fps))
        self._interval_seconds = 1.0 / self._target_fps
        self._next_capture_at = time.perf_counter()
        self._frame_id = 0
        self._latest_frame = None

    def get_latest_frame(self, *, last_seen_id: int = 0, timeout: float = 0.25):
        deadline = time.perf_counter() + max(0.0, float(timeout))
        while True:
            now = time.perf_counter()
            if now < self._next_capture_at:
                wait_time = min(self._next_capture_at - now, max(0.0, deadline - now))
                if wait_time > 0.0:
                    time.sleep(wait_time)
                now = time.perf_counter()
            if now >= deadline and self._frame_id > last_seen_id:
                return self._latest_frame, self._frame_id
            if now >= self._next_capture_at:
                frame = self._frame_grabber.grab()
                captured_at = time.perf_counter()
                self._frame_id += 1
                self._latest_frame = _CapturedFrame(
                    frame_id=self._frame_id,
                    captured_at=captured_at,
                    frame=frame,
                )
                self._next_capture_at = captured_at + self._interval_seconds
                if self._frame_id > last_seen_id:
                    return self._latest_frame, self._frame_id
            if time.perf_counter() >= deadline:
                return None, last_seen_id

    def close(self) -> None:
        close = getattr(self._frame_grabber, "close", None)
        if callable(close):
            close()


@dataclass(slots=True, frozen=True)
class _CapturedFrame:
    frame_id: int
    captured_at: float
    frame: Any


def _write_plot(path: Path, burst_series, profile) -> None:
    import cv2
    import numpy as np

    canvas = np.full((720, 960, 3), 255, dtype=np.uint8)
    origin_x = 120
    origin_y = 600
    width = 760
    height = 460
    cv2.rectangle(canvas, (origin_x, origin_y - height), (origin_x + width, origin_y), (40, 40, 40), 1)
    colors = [(180, 180, 255), (180, 255, 180), (255, 180, 180), (200, 220, 120)]
    for burst_index, burst in enumerate(burst_series):
        previous = None
        color = colors[burst_index % len(colors)]
        max_offset = max(sample.offset_ms for sample in burst.samples) or 1
        for sample in burst.samples:
            x = origin_x + int((sample.offset_ms / max_offset) * width)
            y = origin_y + int(sample.y * 10.0)
            if previous is not None:
                cv2.line(canvas, previous, (x, y), color, 1, cv2.LINE_AA)
            previous = (x, y)
    previous = None
    max_profile_offset = max(1, profile.initial_delay_ms + (profile.sample_count * profile.sample_interval_ms))
    for index, sample_y in enumerate(profile.samples_y):
        offset_ms = profile.initial_delay_ms + (index * profile.sample_interval_ms)
        x = origin_x + int((offset_ms / max_profile_offset) * width)
        y = origin_y + int(sample_y * 10.0)
        if previous is not None:
            cv2.line(canvas, previous, (x, y), (40, 40, 220), 2, cv2.LINE_AA)
        previous = (x, y)
    cv2.putText(canvas, profile.profile_id, (40, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (20, 20, 20), 2, cv2.LINE_AA)
    path.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(path), canvas)


def _select_best_name(votes: list[str]) -> str | None:
    normalized = [
        candidate
        for candidate in normalize_ocr_lines([item for item in votes if isinstance(item, str) and item.strip()])
        if _is_plausible_weapon_name(candidate) and not _looks_like_ammo_label(candidate)
    ]
    if not normalized:
        return None
    if len(normalized) >= 2:
        first = normalized[0]
        second = normalized[1]
        compact_first = "".join(first.split())
        compact_second = "".join(second.split())
        if len(compact_second) <= 4:
            merged_targets = {
                f"{compact_first}{compact_second}",
                f"{compact_first} {compact_second}",
            }
            for candidate in normalized[2:]:
                if candidate in merged_targets or "".join(candidate.split()) in {compact_first + compact_second}:
                    return candidate
    return normalized[0]


def _build_canonical_weapon_id(game: str, display_name: str) -> str:
    return f"{game}-{display_name}"


def _normalize_switch_capture_delays(values: Iterable[int]) -> tuple[int, int]:
    normalized = tuple(sorted({max(600, int(value)) for value in values}))
    if not normalized:
        return (600, 760)
    if len(normalized) == 1:
        return (normalized[0], normalized[0] + 160)
    return normalized[0], normalized[1]


def _utc_timestamp() -> str:
    from datetime import datetime
    from datetime import timezone

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _is_plausible_weapon_name(value: str) -> bool:
    normalized = _require_non_empty_str(value, "value")
    tokens = normalized.split()
    if len(tokens) > 3:
        return False
    if any(token.isdigit() for token in tokens[1:]):
        return False
    compact = "".join(normalized.split())
    if len(compact) < 2:
        return False
    if len(compact) > 24:
        return False
    return any(character.isalpha() for character in compact)


_FULL_FRAME_ROI = NormalizedROI(left=0.0, top=0.0, width=1.0, height=1.0)


def _build_switch_capture_roi(game: str) -> NormalizedROI:
    normalized_game = _require_non_empty_str(game, "game").casefold()
    if normalized_game == "cod20":
        return NormalizedROI(left=0.08, top=0.0, width=0.84, height=0.42)
    if normalized_game == "cod21":
        return NormalizedROI(left=0.12, top=0.0, width=0.82, height=0.40)
    return _FULL_FRAME_ROI


def _looks_like_ammo_label(value: str) -> bool:
    normalized = _require_non_empty_str(value, "value")
    compact = "".join(normalized.split())
    compact_upper = compact.upper()
    if "手枪弹" in normalized or "步枪弹" in normalized:
        return True
    if compact_upper.endswith("BLK") and any(character.isdigit() for character in compact_upper):
        return True
    if compact_upper in {"9MM", "45ACP", "5.56NATO", "7.62NATO"}:
        return True
    return False


def _build_primary_display_roi_bbox(
    roi: NormalizedROI,
    *,
    padding_x: int = 96,
    padding_y: int = 40,
) -> tuple[int, int, int, int]:
    import win32api

    screen_width = int(win32api.GetSystemMetrics(0))
    screen_height = int(win32api.GetSystemMetrics(1))
    left = int(round(roi.left * screen_width)) - padding_x
    top = int(round(roi.top * screen_height)) - padding_y
    right = int(round((roi.left + roi.width) * screen_width)) + padding_x
    bottom = int(round((roi.top + roi.height) * screen_height)) + padding_y
    left = max(0, min(screen_width - 1, left))
    top = max(0, min(screen_height - 1, top))
    right = max(left + 1, min(screen_width, right))
    bottom = max(top + 1, min(screen_height, bottom))
    return (left, top, right, bottom)


def _build_center_region_bbox(*, width: int, height: int) -> tuple[int, int, int, int]:
    import win32api

    screen_width = int(win32api.GetSystemMetrics(0))
    screen_height = int(win32api.GetSystemMetrics(1))
    bounded_width = max(32, min(screen_width, int(width)))
    bounded_height = max(32, min(screen_height, int(height)))
    left = max(0, (screen_width - bounded_width) // 2)
    top = max(0, (screen_height - bounded_height) // 2)
    return (left, top, left + bounded_width, top + bounded_height)


def _require_non_empty_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    result = value.strip()
    if not result:
        raise ValueError(f"{label} must be a non-empty string")
    return result


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
