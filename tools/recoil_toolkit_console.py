from __future__ import annotations

import os
from pathlib import Path
import sys
from typing import Callable
from typing import Iterable
from typing import TextIO

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools import recoil_collector
from tools import recoil_runtime_launcher
from tools import weapon_signature_capture
from vision.recoil_collection.storage import StorageError
from vision.recoil_collection.storage import load_identity_record
from vision.weapon_identity.models import WeaponIdentityRecord


def main(
    argv: Iterable[str] | None = None,
    *,
    stdout: TextIO | None = None,
    stderr: TextIO | None = None,
    input_fn: Callable[[str], str] | None = None,
    collector_main=None,
    signature_capture_main=None,
    runtime_launcher_main=None,
    project_root: Path | None = None,
    signature_dir: Path | None = None,
    profile_dir: Path | None = None,
    state_dir: Path | None = None,
) -> int:
    del argv
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr
    input_fn = input_fn or input
    collector_main = collector_main or recoil_collector.main
    signature_capture_main = signature_capture_main or weapon_signature_capture.main
    runtime_launcher_main = runtime_launcher_main or recoil_runtime_launcher.main

    root = project_root or PROJECT_ROOT
    signature_dir = signature_dir or _resolve_path_env("RECOIL_SIGNATURE_DIR", root / "artifacts" / "weapon_signatures")
    profile_dir = profile_dir or _resolve_path_env("RECOIL_PROFILE_DIR", root / "artifacts" / "recoil_profiles")
    state_dir = state_dir or _resolve_path_env("RECOIL_STATE_DIR", root / "artifacts" / "recoil_state")
    signature_dir.mkdir(parents=True, exist_ok=True)
    profile_dir.mkdir(parents=True, exist_ok=True)
    state_dir.mkdir(parents=True, exist_ok=True)

    _print_menu(stdout)
    action = _read_input(input_fn, "Choose [1-4]: ").strip()

    if action == "1":
        return _run_record_identity(
            input_fn=input_fn,
            stdout=stdout,
            stderr=stderr,
            signature_capture_main=signature_capture_main,
            signature_dir=signature_dir,
        )
    if action == "2":
        return _run_collect_recoil(
            input_fn=input_fn,
            stdout=stdout,
            stderr=stderr,
            collector_main=collector_main,
            signature_dir=signature_dir,
            profile_dir=profile_dir,
        )
    if action == "3":
        return _run_runtime(
            input_fn=input_fn,
            stdout=stdout,
            stderr=stderr,
            runtime_launcher_main=runtime_launcher_main,
            signature_dir=signature_dir,
            profile_dir=profile_dir,
            state_dir=state_dir,
            recognizer_only=False,
        )
    if action == "4":
        return _run_runtime(
            input_fn=input_fn,
            stdout=stdout,
            stderr=stderr,
            runtime_launcher_main=runtime_launcher_main,
            signature_dir=signature_dir,
            profile_dir=profile_dir,
            state_dir=state_dir,
            recognizer_only=True,
        )

    stdout.write("Invalid selection.\n")
    stdout.flush()
    return 0


def _run_record_identity(
    *,
    input_fn: Callable[[str], str],
    stdout: TextIO,
    stderr: TextIO,
    signature_capture_main,
    signature_dir: Path,
) -> int:
    game = _prompt_game(input_fn, stdout)
    canonical_weapon_id = _require_text(
        _read_input(input_fn, "Canonical weapon id (example: cod22-黑色组织传奇): "),
        "Canonical weapon id is required.",
        stdout,
    )
    if canonical_weapon_id is None:
        return 0
    display_name = _require_text(
        _read_input(input_fn, "Display name (exact HUD text): "),
        "Display name is required.",
        stdout,
    )
    if display_name is None:
        return 0
    weapon_family = _read_input(input_fn, "Weapon family (default assault_rifle): ").strip() or "assault_rifle"
    argv = [
        "--game",
        game,
        "--canonical-weapon-id",
        canonical_weapon_id,
        "--display-name",
        display_name,
        "--weapon-family",
        weapon_family,
        "--signature-dir",
        str(signature_dir),
        "--identity-only",
    ]
    return int(signature_capture_main(argv=argv, stdout=stdout, stderr=stderr))


def _run_collect_recoil(
    *,
    input_fn: Callable[[str], str],
    stdout: TextIO,
    stderr: TextIO,
    collector_main,
    signature_dir: Path,
    profile_dir: Path,
) -> int:
    game = _prompt_game(input_fn, stdout)
    identity_records = _load_identity_records(signature_dir=signature_dir, game=game)
    if not identity_records:
        stdout.write(f"No weapon identity records found for {game}.\n")
        stdout.write("Run option 1 first to record the current HUD weapon name.\n")
        stdout.flush()
        return 0

    selected_identity = _prompt_identity_record(input_fn, stdout, identity_records)
    if selected_identity is None:
        return 0

    mode = _prompt_recoil_mode(input_fn, stdout)
    startup_delay = _prompt_startup_delay(input_fn, stdout)
    summary_path = profile_dir / f"{game}-{mode}-latest-summary.json"

    stdout.write("\nRecoil capture instructions:\n")
    stdout.write(f"- Weapon: {selected_identity.display_name} ({selected_identity.canonical_weapon_id})\n")
    stdout.write("- Stay standing.\n")
    stdout.write("- Switch back to the game before the countdown ends.\n")
    stdout.write("- Fire 3-4 clean bursts with RT or RB, and fully release between bursts.\n\n")
    stdout.flush()

    argv = [
        "--game",
        game,
        "--mode",
        mode,
        "--standing-only",
        "--canonical-weapon-id",
        selected_identity.canonical_weapon_id,
        "--startup-delay",
        _format_float(startup_delay),
        "--profile-dir",
        str(profile_dir),
        "--signature-dir",
        str(signature_dir),
        "--output",
        str(summary_path),
    ]
    return int(collector_main(argv=argv, stdout=stdout, stderr=stderr))


def _run_runtime(
    *,
    input_fn: Callable[[str], str],
    stdout: TextIO,
    stderr: TextIO,
    runtime_launcher_main,
    signature_dir: Path,
    profile_dir: Path,
    state_dir: Path,
    recognizer_only: bool,
) -> int:
    game = _prompt_game(input_fn, stdout)
    identity_records = _load_identity_records(signature_dir=signature_dir, game=game)
    if not identity_records:
        stdout.write(f"No weapon identity records found for {game}.\n")
        stdout.write("Run option 1 first to record the current HUD weapon name.\n")
        stdout.flush()
        return 0

    argv = [
        "--game",
        game,
        "--profile-dir",
        str(profile_dir),
        "--signature-dir",
        str(signature_dir),
        "--state-file",
        str(state_dir / f"{game}-latest-state.json"),
    ]
    if recognizer_only:
        argv.append("--recognizer-only")
    else:
        fire_output = _prompt_fire_output(input_fn, stdout)
        argv.extend(
            [
                "--controller-mode",
                "gamepad",
                "--auto-fire-output",
                fire_output,
                "--vision-backend",
                os.environ.get("RECOIL_VISION_BACKEND", "native"),
            ]
        )

    return int(runtime_launcher_main(argv=argv, stdout=stdout, stderr=stderr))


def _print_menu(stdout: TextIO) -> None:
    stdout.write("\n")
    stdout.write("==================================\n")
    stdout.write("COD Recoil Toolkit (Python Console)\n")
    stdout.write("==================================\n")
    stdout.write("1. Record weapon identity\n")
    stdout.write("2. Collect recoil profile\n")
    stdout.write("3. Start Y-switch text runtime + gamepad\n")
    stdout.write("4. Start continuous recognizer only (debug)\n\n")
    stdout.flush()


def _prompt_game(input_fn: Callable[[str], str], stdout: TextIO) -> str:
    stdout.write("Select game:\n")
    stdout.write("1. COD20\n")
    stdout.write("2. COD21\n")
    stdout.write("3. COD22\n")
    stdout.flush()
    choice = _read_input(input_fn, "Choose [1-3] (default 3): ").strip()
    return {
        "1": "cod20",
        "2": "cod21",
        "3": "cod22",
        "": "cod22",
    }.get(choice, "cod22")


def _prompt_recoil_mode(input_fn: Callable[[str], str], stdout: TextIO) -> str:
    stdout.write("\nSelect recoil mode:\n")
    stdout.write("1. ADS\n")
    stdout.write("2. Hipfire\n")
    stdout.flush()
    choice = _read_input(input_fn, "Choose [1-2] (default 1): ").strip()
    if choice == "2":
        return "hipfire"
    return "ads"


def _prompt_fire_output(input_fn: Callable[[str], str], stdout: TextIO) -> str:
    stdout.write("\nSelect AutoFire output:\n")
    stdout.write("1. RB\n")
    stdout.write("2. RT\n")
    stdout.flush()
    choice = _read_input(input_fn, "Choose [1-2] (default 1): ").strip()
    if choice == "2":
        return "RT"
    return "RB"


def _prompt_startup_delay(input_fn: Callable[[str], str], stdout: TextIO) -> float:
    stdout.write("\nCollector startup delay gives you time to alt-tab back into the game.\n")
    stdout.flush()
    raw_value = _read_input(input_fn, "Startup delay seconds (default 3): ").strip()
    if raw_value == "":
        return 3.0
    try:
        value = float(raw_value)
    except ValueError:
        stdout.write("Invalid delay. Using default 3 seconds.\n")
        stdout.flush()
        return 3.0
    if value < 0.0:
        stdout.write("Negative delay is not allowed. Using default 3 seconds.\n")
        stdout.flush()
        return 3.0
    return value


def _prompt_identity_record(
    input_fn: Callable[[str], str],
    stdout: TextIO,
    records: tuple[WeaponIdentityRecord, ...],
) -> WeaponIdentityRecord | None:
    stdout.write("\nSelect weapon identity:\n")
    for index, record in enumerate(records, start=1):
        stdout.write(f"{index}. {record.display_name} [{record.canonical_weapon_id}]\n")
    stdout.flush()
    raw_choice = _read_input(input_fn, f"Choose [1-{len(records)}]: ").strip()
    try:
        index = int(raw_choice)
    except ValueError:
        stdout.write("Invalid weapon selection.\n")
        stdout.flush()
        return None
    if index < 1 or index > len(records):
        stdout.write("Invalid weapon selection.\n")
        stdout.flush()
        return None
    return records[index - 1]


def _load_identity_records(*, signature_dir: Path, game: str) -> tuple[WeaponIdentityRecord, ...]:
    if not signature_dir.exists() or not signature_dir.is_dir():
        return ()
    records: list[WeaponIdentityRecord] = []
    for path in sorted(signature_dir.glob("identity-*.json")):
        try:
            record = load_identity_record(path)
        except (StorageError, ValueError, OSError):
            continue
        if record.game == game:
            records.append(record)
    return tuple(records)


def _require_text(value: str, error_message: str, stdout: TextIO) -> str | None:
    result = value.strip()
    if result:
        return result
    stdout.write(error_message + "\n")
    stdout.flush()
    return None


def _resolve_path_env(env_name: str, default: Path) -> Path:
    raw_value = os.environ.get(env_name)
    if raw_value is None or not raw_value.strip():
        return default
    return Path(raw_value).expanduser()


def _format_float(value: float) -> str:
    integer_value = int(value)
    if float(integer_value) == float(value):
        return str(integer_value)
    return f"{value:.3f}".rstrip("0").rstrip(".")


def _read_input(input_fn: Callable[[str], str], prompt: str) -> str:
    try:
        return input_fn(prompt)
    except EOFError:
        return ""


if __name__ == "__main__":
    raise SystemExit(main())
