"""Replay native observations with explicit synthetic controls; never opens devices."""
import argparse,json,subprocess,csv
from pathlib import Path
root=Path(__file__).resolve().parents[2];baseline=root/'runs/new-video-study-20261025'
p=argparse.ArgumentParser();p.add_argument('--vision-root',type=Path,default=baseline);p.add_argument('--output',type=Path,default=baseline);args=p.parse_args()
out=args.output;out.mkdir(parents=True,exist_ok=True);vision=args.vision_root
exe=baseline/'controller-build/Release/controller_study.exe'
profile=root/'profiles/library/p_cd8d772a4f4f4335905ab9b86e3e5741.toml'
clips=json.loads((baseline/'selected.json').read_text(encoding='utf-8'))
# KILLCAM is retained as a stress input, not user gameplay ground truth.
(out/'open-manifest.txt').write_text(''.join(f'{c["id"]} {(vision/c["id"]/"vision.records").as_posix()} 0\n' for c in clips))
windows=[('friend_overlap','v01',11.5),('reload_weapon','v06',5.1),
 ('statue','v07',14.8),('cue_return','v07',22.8),('head_fragment','v07',43.8),
 ('background_growth','v07',87.8),('pose_scale','v07',180.7),
 ('nearpass','v07',241.8),('clipped_body','v08',18.0),
 ('scale_recovery','v08',10.7),('clear_tracking','v07',60.0),('ironsight_control','v08',35.0)]
(out/'closed-manifest.txt').write_text(''.join(f'{name} {(vision/clip/"vision.records").as_posix()} {start}\n' for name,clip,start in windows))
results=[]
for mode,seeds in [('open',[20261025]),('closed',[20261025,73129,880301,407771])]:
 for seed in seeds:
  dest=out/f'{mode}-{seed}'
  run=subprocess.run([str(exe),str(profile),str(out/f'{mode}-manifest.txt'),str(dest),str(seed),mode,'-','0'],capture_output=True,text=True)
  print(mode,seed,run.returncode,run.stdout.strip(),run.stderr.strip(),flush=True)
  if run.returncode not in (0,2):raise RuntimeError(run.stderr)
  rows=list(csv.DictReader((dest/'cases.csv').open()))
  results.append(dict(mode=mode,seed=seed,returncode=run.returncode,cases=len(rows),ticks=sum(int(r['ticks']) for r in rows),violations=sum(int(r['violations']) for r in rows)))
(out/'simulation-summary.json').write_text(json.dumps(results,indent=2))
