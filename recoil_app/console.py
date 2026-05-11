from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time
from typing import Iterable

from controllers.gamepad.physical_input import PygamePhysicalGamepadReader

from .runtime import GamepadRecoilBridge
from .runtime import IdentityStore
from .runtime import RecoilAppConfig
from .runtime import RecoilProfileStore
from .runtime import RecoilRuntime


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Standalone recoil_app console.")
    parser.add_argument("--game", choices=("cod20", "cod21", "cod22"))
    parser.add_argument("--mode", choices=("record", "recoil"))
    parser.add_argument("--weapon-dir", type=Path)
    parser.add_argument("--profile-dir", type=Path)
    parser.add_argument("--state-path", type=Path)
    parser.add_argument("--plot-dir", type=Path)
    return parser


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    return build_parser().parse_args(list(argv) if argv is not None else None)


def build_runtime_config(
    *,
    root: Path,
    game: str,
    mode: str,
    weapon_dir: Path | None = None,
    profile_dir: Path | None = None,
    state_path: Path | None = None,
    plot_dir: Path | None = None,
    switch_capture_delays_ms: tuple[int, ...] | None = None,
) -> RecoilAppConfig:
    return RecoilAppConfig(
        game=game,
        mode=mode,
        weapon_dir=str(weapon_dir or (root / "artifacts" / "recoil_app" / "weapons")),
        profile_dir=str(profile_dir or (root / "artifacts" / "recoil_profiles")),
        state_path=str(state_path or (root / "artifacts" / "recoil_app" / "current_weapon.json")),
        plot_dir=str(plot_dir or (root / "artifacts" / "recoil_plots")),
        startup_delay_ms=switch_capture_delays_ms or (600, 760),
    )


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv)
    game = args.game or _prompt_game()
    mode = args.mode or _prompt_mode()
    root = Path.cwd()
    config = build_runtime_config(
        root=root,
        game=game,
        mode=mode,
        weapon_dir=args.weapon_dir,
        profile_dir=args.profile_dir,
        state_path=args.state_path,
        plot_dir=args.plot_dir,
    )
    runtime = RecoilRuntime(
        game=config.game,
        mode=config.mode,
        identity_store=IdentityStore(Path(config.weapon_dir)),
        profile_store=RecoilProfileStore(Path(config.profile_dir)),
        state_path=Path(config.state_path),
        plot_dir=Path(config.plot_dir),
        switch_capture_delays_ms=config.startup_delay_ms,
        stdout=sys.stdout,
    )
    bridge = GamepadRecoilBridge(runtime)

    import pygame

    pygame.init()
    pygame.joystick.init()
    if pygame.joystick.get_count() == 0:
        print("[Recoil] No physical gamepad detected.")
        return 1
    joystick = pygame.joystick.Joystick(0)
    joystick.init()
    reader = PygamePhysicalGamepadReader(pygame_module=pygame, joystick=joystick)
    print(f"[Recoil] Started in {config.mode} mode for {config.game}. Press Ctrl+C to stop.")
    try:
        while True:
            reader.pump()
            buttons = reader.read_buttons()
            left_trigger, _right_trigger = reader.read_trigger_values()
            bridge.handle_buttons(buttons)
            bridge.handle_fire_state(
                is_firing=reader.read_right_fire_pressed(),
                is_aiming=left_trigger > 10,
            )
            time.sleep(1.0 / 60.0)
    except KeyboardInterrupt:
        return 0
    finally:
        quit_joystick = getattr(joystick, "quit", None)
        if callable(quit_joystick):
            quit_joystick()
        runtime.close()
        pygame.quit()


def _prompt_game() -> str:
    print("Select game:")
    print("1. COD20")
    print("2. COD21")
    print("3. COD22")
    choice = input("Choose [1-3] (default 3): ").strip()
    return {"1": "cod20", "2": "cod21", "3": "cod22", "": "cod22"}.get(choice, "cod22")


def _prompt_mode() -> str:
    print("Select mode:")
    print("1. Record mode")
    print("2. Recoil mode")
    choice = input("Choose [1-2] (default 2): ").strip()
    return {"1": "record", "2": "recoil", "": "recoil"}.get(choice, "recoil")


if __name__ == "__main__":
    raise SystemExit(main())
