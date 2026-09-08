"""Build the standalone Interception -> FakerInput relay; never install drivers."""
import os
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CMAKE = Path(r'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
BUILD = ROOT / 'artifacts/mouse_link/virtual-build'

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=BUILD)
    parser.add_argument('--config', choices=['Release', 'RelWithDebInfo'], default='Release')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    # The host can expose both PATH and Path. MSBuild's case-insensitive
    # environment dictionary rejects duplicate names; normalize the child only.
    environment = {key.upper(): value for key, value in os.environ.items()}
    commands = [
        [str(CMAKE), '-S', str(ROOT / 'native/mouse_link'), '-B', str(build),
         '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DMOUSE_LISTENER_ONLY=ON'],
        [str(CMAKE), '--build', str(build), '--config', args.config, '--parallel', '1'],
        [str(CMAKE.parent / 'ctest.exe'), '--test-dir', str(build), '-C', args.config, '--output-on-failure'],
    ]
    for command in commands:
        result = subprocess.run(command, env=environment, cwd=ROOT)
        if result.returncode:
            raise SystemExit(result.returncode)
    print('Ready:', build / args.config / 'mouse_virtual_relay.exe')
    print('Build/tests did not install drivers, capture physical input, or submit virtual input.')
