import tempfile
import unittest
from pathlib import Path


def _load_console_module():
    import importlib

    try:
        return importlib.import_module("recoil_app.console")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing recoil_app console module: {exc}") from exc


class RecoilAppConsoleTests(unittest.TestCase):
    def test_build_runtime_config_uses_in_memory_defaults_without_creating_config_file(self):
        console = _load_console_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)

            config = console.build_runtime_config(
                root=temp_path,
                game="cod21",
                mode="record",
            )

            self.assertEqual(config.game, "cod21")
            self.assertEqual(config.mode, "record")
            self.assertEqual(Path(config.weapon_dir), temp_path / "artifacts" / "recoil_app" / "weapons")
            self.assertEqual(Path(config.profile_dir), temp_path / "artifacts" / "recoil_profiles")
            self.assertEqual(Path(config.state_path), temp_path / "artifacts" / "recoil_app" / "current_weapon.json")
            self.assertEqual(Path(config.plot_dir), temp_path / "artifacts" / "recoil_plots")
            self.assertFalse((temp_path / "artifacts" / "recoil_app" / "config.json").exists())

    def test_build_runtime_config_clamps_short_legacy_switch_delays(self):
        console = _load_console_module()

        config = console.build_runtime_config(
            root=Path("D:/tmp/recoil-app"),
            game="cod22",
            mode="recoil",
            switch_capture_delays_ms=(150, 240, 340, 460),
        )

        self.assertEqual(config.startup_delay_ms, (600, 760))


if __name__ == "__main__":
    unittest.main()
