"""Extract the previous clean selector without touching either worktree."""
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[2]
out = root / 'runs/vision-perf-20261023/old'
(out / 'include/vision_native').mkdir(parents=True, exist_ok=True)
for name in ('target_selector.cpp', 'target_selector_fusion.cpp', 'cue_identity.cpp'):
    (out / name).write_bytes(subprocess.check_output(['git', 'show', '71f9104:native/vision_native/src/' + name], cwd=root))
for name in ('target_selector.h', 'cue_identity.h', 'types.h'):
    (out / 'include/vision_native' / name).write_bytes(subprocess.check_output(['git', 'show', '71f9104:native/vision_native/include/vision_native/' + name], cwd=root))
print(out)
