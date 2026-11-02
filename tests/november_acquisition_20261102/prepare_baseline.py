"""Export this incident's pre-fix dev baseline, without checkout/reset."""
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'runs/incident-20261102/fix/baseline'
rev='e03908193610d5ea5996d21f5a448695ab18f9e8'
names=subprocess.check_output(['git','ls-tree','-r','--name-only',rev,'native/vision_native/include','native/pipeline_contract','native/common_native'],cwd=ROOT,text=True).splitlines()
names += [f'native/vision_native/src/{s}.cpp' for s in ['target_selector','target_selector_fusion','cue_identity']]
for name in names:
 p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(subprocess.check_output(['git','show',rev+':'+name],cwd=ROOT))
(OUT/'revision.txt').write_text(rev+'\n');print(rev,len(names))
