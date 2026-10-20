"""Extract immutable production baselines without changing branches/worktrees."""
import io
import subprocess
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
run = root / 'runs/ads-cooperation-20261019'
for name, revision in {'pre': '6496f84', 'before': '998275a'}.items():
    destination = run / name
    archive = zipfile.ZipFile(io.BytesIO(subprocess.check_output(
        ['git', 'archive', '--format=zip', revision, 'native'], cwd=root)))
    for entry in archive.infolist():
        if Path(entry.filename).suffix not in {'.h', '.cpp', '.inc', '.json', '.cmake'}:
            continue
        path = destination / entry.filename
        if not path.resolve().is_relative_to(destination.resolve()):
            raise ValueError('archive path escapes baseline directory')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(archive.read(entry))
    print(f'{name}: {revision} -> {destination}')
