"""Freeze a pre-region revision and add an offline-only seed ceiling."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
revision = sys.argv[1] if len(sys.argv)>1 else 'f4b9cbe'
out = root / (sys.argv[2] if len(sys.argv)>2 else 'runs/cue-region-20261023/reference')
out.mkdir(parents=True, exist_ok=False)
def read(path):
    return subprocess.check_output(['git','show',revision+':'+path],cwd=root).decode('utf-8')

header = read('native/vision_native/include/vision_native/cue_identity.h')
header = header.replace('class CueDetector', 'class ReferenceCueDetector')
header = header[:header.index('struct CueObservation')] + header[header.index('// Independent'):]
header = header[:header.index('// A missing/ambiguous')] + 'extern int region_seed_bottom;\n} // namespace vision_native\n'
header = header.replace('#include "vision_native/types.h"', '#include "vision_native/cue_identity.h"')
(out / 'reference.h').write_text(header, encoding='utf-8')
source = read('native/vision_native/src/cue_identity.cpp')
(out / 'saved_second_version.cpp').write_text(source, encoding='utf-8')
source = source[:source.index('void CueIdentity::reset()')] + '} // namespace vision_native\n'
source = source.replace('"vision_native/cue_identity.h"', '"reference.h"').replace('CueDetector::', 'ReferenceCueDetector::')
source = source.replace('namespace vision_native {', 'namespace vision_native {\nint region_seed_bottom=512;', 1)
needle = 'seed_y<height;'
assert source.count(needle) == 1
# Limit new region discovery only. Keep full image bounds, full component
# expansion and the true reticle centre; cropping height would change all three.
source = source.replace(needle, 'seed_y<std::min(height,region_seed_bottom);')
(out / 'reference.cpp').write_text(source, encoding='utf-8')
header = read('native/vision_native/include/vision_native/target_selector.h').replace('VisionTargetSelector', 'ReferenceVisionTargetSelector').replace('CueDetector', 'ReferenceCueDetector').replace('"vision_native/cue_identity.h"', '"reference.h"')
(out / 'reference_selector.h').write_text(header, encoding='utf-8')
for name in ['target_selector.cpp', 'target_selector_fusion.cpp']:
    source = read('native/vision_native/src/' + name).replace('VisionTargetSelector', 'ReferenceVisionTargetSelector').replace('"vision_native/target_selector.h"', '"reference_selector.h"')
    (out / name.replace('target_', 'reference_')).write_text(source, encoding='utf-8')
print(out)
