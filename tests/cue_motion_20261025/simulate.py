"""Current native chain, prerecorded observations and explicit synthetic input."""
import csv,json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'runs/cue-motion-20261025/controller-release';out.mkdir(exist_ok=True)
vision=root/'runs/cue-motion-20261025/release-hdr'
exe=root/'runs/new-video-study-20261025/controller-build/Release/controller_study.exe'
profile=root/'profiles/library/p_cd8d772a4f4f4335905ab9b86e3e5741.toml'
(out/'open.txt').write_text(''.join(f'{q} {(vision/q/"vision.records").as_posix()} 0\n' for q in ['q1','q2']))
windows=[('wall_occlusion','q1',6),('aim_occlusion','q1',20),('near_jump','q1',28),('jump_pose','q1',35.7),('slide_recovery','q1',54),('second_jump','q2',7.7),('mannequin','q2',12.7),('second_slide','q2',18.5)]
(out/'closed.txt').write_text(''.join(f'{n} {(vision/q/"vision.records").as_posix()} {t}\n' for n,q,t in windows))
results=[]
for mode,seeds in [('open',[20261025]),('closed',[20261025,73129,880301,407771])]:
 for seed in seeds:
  dest=out/f'{mode}-{seed}'
  r=subprocess.run([str(exe),str(profile),str(out/f'{mode}.txt'),str(dest),str(seed),mode,'-','0'],capture_output=True,text=True)
  assert r.returncode in (0,2),(r.returncode,r.stderr)
  rows=list(csv.DictReader((dest/'cases.csv').open()))
  result=dict(mode=mode,seed=seed,returncode=r.returncode,cases=len(rows),ticks=sum(int(x['ticks']) for x in rows),violations=sum(int(x['violations']) for x in rows))
  results.append(result);print(result,flush=True)
(out/'summary.json').write_text(json.dumps(results,indent=2))
