"""Freeze current selector/color code before editing; offline comparison only."""
from pathlib import Path
import subprocess,sys
root = Path(__file__).resolve().parents[2]
revision=sys.argv[1] if len(sys.argv)>1 else 'HEAD'
def read(path):
    return subprocess.check_output(['git','show',revision+':'+str(path.relative_to(root)).replace('\\','/')],cwd=root).decode('utf-8')
out = root / 'runs/palette-20261023/reference'
out.mkdir(parents=True, exist_ok=False)
inc = root / 'native/vision_native/include/vision_native'
src = root / 'native/vision_native/src'
h = read(inc/'cue_identity.h')
h = '#pragma once\n#include "vision_native/cue_identity.h"\nnamespace vision_native {\n' + h[h.index('class CueDetector'):h.index('// A missing/ambiguous')] + '\n}\n'
(out/'reference.h').write_text(h.replace('CueDetector','ReferenceCueDetector'))
s = read(src/'cue_identity.cpp')
s = s[:s.index('void CueIdentity::reset()')] + '}\n'
s = s.replace('const std::vector<CueObservation>& CueDetector::detect',
    'int reference_enemy_color(const unsigned char* p) { return enemy_color(p,PixelFormat::BGRA8); }\nconst std::vector<CueObservation>& CueDetector::detect')
(out/'reference.cpp').write_text(s.replace('"vision_native/cue_identity.h"','"reference.h"').replace('CueDetector::','ReferenceCueDetector::'))
h = read(inc/'target_selector.h').replace('VisionTargetSelector','ReferenceVisionTargetSelector').replace('CueDetector','ReferenceCueDetector')
(out/'reference_selector.h').write_text(h.replace('"vision_native/cue_identity.h"','"reference.h"'))
for name in ['target_selector.cpp','target_selector_fusion.cpp']:
    s=read(src/name).replace('VisionTargetSelector','ReferenceVisionTargetSelector').replace('"vision_native/target_selector.h"','"reference_selector.h"')
    if name=='target_selector.cpp':
        s=s.replace('void ReferenceVisionTargetSelector::set_detection_policy',
            'bool reference_friendly_color(const unsigned char* p) { float h,s,v;rgb_to_opencv_hsv(p[2],p[1],p[0],h,s,v);return is_friendly_hsv(h,s,v); }\nvoid ReferenceVisionTargetSelector::set_detection_policy')
    (out/('reference_selector.cpp' if name=='target_selector.cpp' else 'reference_selector_fusion.cpp')).write_text(s)
print(out)
