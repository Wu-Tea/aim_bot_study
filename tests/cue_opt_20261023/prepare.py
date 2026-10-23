"""Freeze the pre-optimization detector/selector under distinct test-only names."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
revision = sys.argv[1] if len(sys.argv) > 1 else '4c19029'
out = root / (sys.argv[2] if len(sys.argv) > 2 else 'runs/cue-opt-20261023/baseline')
out.mkdir(parents=True, exist_ok=True)
def read(path):
    return subprocess.check_output(['git', 'show', revision + ':' + path], cwd=root).decode()
header = read('native/vision_native/include/vision_native/cue_identity.h').replace('class CueDetector', 'class ReferenceCueDetector')
header = header[:header.index('struct CueObservation')] + header[header.index('// Independent'):]
header = header[:header.index('// A missing/ambiguous')] + '} // namespace vision_native\n'
header = header.replace('#include "vision_native/types.h"', '#include "vision_native/cue_identity.h"')
(out / 'reference.h').write_text(header, encoding='utf-8')
source = read('native/vision_native/src/cue_identity.cpp')
source = source[:source.index('void CueIdentity::reset()')] + '} // namespace vision_native\n'
source = source.replace('"vision_native/cue_identity.h"', '"reference.h"').replace('CueDetector::', 'ReferenceCueDetector::').replace('enemy_color', 'reference_enemy_color')
source = source.replace('kMinimumCueExtent', 'kReferenceMinimumCueExtent')
(out / 'reference.cpp').write_text(source, encoding='utf-8')
header = read('native/vision_native/include/vision_native/target_selector.h').replace('VisionTargetSelector', 'ReferenceVisionTargetSelector').replace('CueDetector', 'ReferenceCueDetector').replace('"vision_native/cue_identity.h"', '"reference.h"')
(out / 'reference_selector.h').write_text(header, encoding='utf-8')
for name, dest in [('target_selector.cpp', 'reference_selector.cpp'), ('target_selector_fusion.cpp', 'reference_selector_fusion.cpp')]:
    source = read('native/vision_native/src/' + name).replace('VisionTargetSelector', 'ReferenceVisionTargetSelector').replace('"vision_native/target_selector.h"', '"reference_selector.h"')
    (out / dest).write_text(source, encoding='utf-8')
print(out)
