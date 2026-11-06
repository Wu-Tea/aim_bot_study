import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'runs/person-cue-policy-20261106/baseline'
rev=subprocess.check_output(['git','rev-parse','64d930b'],cwd=ROOT,text=True).strip()
names=subprocess.check_output(['git','ls-tree','-r','--name-only',rev,'native/vision_native/include','native/pipeline_contract','native/common_native'],cwd=ROOT,text=True).splitlines()
names += [f'native/vision_native/src/{s}.cpp' for s in ['target_selector','target_selector_fusion','cue_identity']]
for name in names:
 p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(subprocess.check_output(['git','show',rev+':'+name],cwd=ROOT))
(OUT/'revision.txt').write_text(rev+'\n');print(rev,len(names))
