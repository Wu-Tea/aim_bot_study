from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Iterable
from typing import Mapping
from typing import TextIO

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_STATE_ROOT = PROJECT_ROOT / "artifacts" / "recoil_state"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Launch the recoil recognizer and gamepad runtime together.")
    parser.add_argument("--game", choices=("cod20", "cod21", "cod22"), required=True)
    parser.add_argument("--profile-dir", type=Path, required=True)
    parser.add_argument("--signature-dir", type=Path, required=True)
    parser.add_argument("--state-file", type=Path)
    parser.add_argument("--recognizer-fps", type=int, default=20)
    parser.add_argument("--controller-mode", choices=("gamepad", "kbm_to_gamepad", "mouse"), default="gamepad")
    parser.add_argument("--auto-fire-output", choices=("RB", "RT"), default="RB")
    parser.add_argument("--vision-backend", choices=("python", "native"), default=os.environ.get("VISION_BACKEND", "native"))
    parser.add_argument("--recognizer-only", action="store_true")
    return parser


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    if args.recognizer_fps <= 0:
        parser.error("--recognizer-fps must be positive")
    return args


def resolve_state_file(*, game: str, state_file: Path | None, root_dir: Path = DEFAULT_STATE_ROOT) -> Path:
    if state_file is not None:
        return state_file
    return root_dir / f"{game}-latest-state.json"


def build_recognizer_command(
    *,
    game: str,
    profile_dir: Path,
    signature_dir: Path,
    state_file: Path,
    recognizer_fps: int,
) -> list[str]:
    return [
        sys.executable,
        str(PROJECT_ROOT / "tools" / "weapon_recognizer.py"),
        "--game",
        game,
        "--profile-dir",
        str(profile_dir),
        "--signature-dir",
        str(signature_dir),
        "--fps",
        str(recognizer_fps),
        "--capture-mode",
        "fullscreen",
        "--latest-state-file",
        str(state_file),
    ]


def build_controller_command(
    *,
    controller_mode: str,
    auto_fire_output: str,
    vision_backend: str,
) -> list[str]:
    return [
        sys.executable,
        str(PROJECT_ROOT / "main.py"),
        "--controller-mode",
        controller_mode,
        "--auto-fire-output",
        auto_fire_output,
        "--vision-backend",
        vision_backend,
    ]


def build_controller_env(
    *,
    base_env: Mapping[str, str] | None,
    profile_dir: Path,
    state_file: Path,
    vision_backend: str,
) -> dict[str, str]:
    env = dict(base_env or os.environ)
    env["RECOIL_PROFILE_DIR"] = str(profile_dir)
    env["RECOIL_RECOGNIZER_STATE_PATH"] = str(state_file)
    env["VISION_BACKEND"] = vision_backend
    return env


def main(
    argv: Iterable[str] | None = None,
    *,
    stdout: TextIO | None = None,
    stderr: TextIO | None = None,
    popen_factory=None,
    run_process=None,
) -> int:
    args = parse_args(argv)
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr
    popen_factory = popen_factory or subprocess.Popen
    run_process = run_process or _run_process

    state_file = resolve_state_file(game=args.game, state_file=args.state_file)
    state_file.parent.mkdir(parents=True, exist_ok=True)
    recognizer_command = build_recognizer_command(
        game=args.game,
        profile_dir=args.profile_dir,
        signature_dir=args.signature_dir,
        state_file=state_file,
        recognizer_fps=args.recognizer_fps,
    )
    recognizer_process = popen_factory(
        recognizer_command,
        cwd=str(PROJECT_ROOT),
        env=dict(os.environ),
    )
    payload = {
        "type": "recoil_runtime_launcher",
        "game": args.game,
        "profile_dir": str(args.profile_dir),
        "signature_dir": str(args.signature_dir),
        "state_file": str(state_file),
        "recognizer_only": args.recognizer_only,
    }
    stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
    stdout.flush()

    if args.recognizer_only:
        return 0

    controller_env = build_controller_env(
        base_env=os.environ,
        profile_dir=args.profile_dir,
        state_file=state_file,
        vision_backend=args.vision_backend,
    )
    controller_command = build_controller_command(
        controller_mode=args.controller_mode,
        auto_fire_output=args.auto_fire_output,
        vision_backend=args.vision_backend,
    )

    try:
        result = run_process(
            controller_command,
            cwd=str(PROJECT_ROOT),
            env=controller_env,
        )
        return int(getattr(result, "returncode", 0))
    except KeyboardInterrupt:
        return 0
    except (FileNotFoundError, OSError, ValueError, subprocess.SubprocessError) as exc:
        print(str(exc), file=stderr)
        return 1
    finally:
        _stop_process(recognizer_process)


def _run_process(command: list[str], *, cwd: str, env: dict[str, str]):
    return subprocess.run(command, cwd=cwd, env=env, check=False)


def _stop_process(process) -> None:
    terminate = getattr(process, "terminate", None)
    wait = getattr(process, "wait", None)
    kill = getattr(process, "kill", None)
    try:
        if callable(terminate):
            terminate()
        if callable(wait):
            wait(timeout=2.0)
    except Exception:
        if callable(kill):
            kill()


if __name__ == "__main__":
    raise SystemExit(main())
