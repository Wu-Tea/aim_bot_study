"""Interleave old/new selector builds on identical original pixels and detections."""
import argparse,csv,json,struct,subprocess,statistics
from pathlib import Path
root=Path(__file__).resolve().parents[2];base=root/'runs/new-video-study-20261025'
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=root/'runs/new-video-fix-20261025/performance-final');args=parser.parse_args();out=args.output
out.mkdir(parents=True,exist_ok=True)
clips={c['id']:c for c in json.loads((base/'selected.json').read_text(encoding='utf-8'))}
exes={'before':root/'runs/new-video-fix-20261025/before/replay.exe','after':root/'runs/ironsight-cue-drift-20261025/build/Release/replay.exe'}
summary=[]
for round in range(3):
 for clip in ['v01','v06','v08']:
  rows=[json.loads(s) for s in (base/clip/'vision.jsonl').read_text().splitlines()]
  timestamps=list(map(int,(base/clip/'timestamps.txt').read_text().split()))
  order=['before','after'] if round%2==0 else ['after','before']
  logs=[];procs=[]
  for name in order:
   log=(out/f'{clip}-{round}-{name}.txt').open('wb');logs.append(log)
   procs.append(subprocess.Popen([str(exes[name]),str(out/f'{clip}-{round}-{name}.csv')],stdin=subprocess.PIPE,stdout=log,stderr=log))
  with (out/f'{clip}-{round}-decode.txt').open('wb') as log:
   dec=subprocess.Popen(['ffmpeg','-v','error','-i',clips[clip]['path'],'-vf','crop=640:512','-fps_mode','passthrough','-f','rawvideo','-pix_fmt','bgra','pipe:1'],stdout=subprocess.PIPE,stderr=log)
   for row,ns in zip(rows,timestamps):
    pix=dec.stdout.read(640*512*4);assert len(pix)==640*512*4
    ds=[d for d in row['detections'] if d['class']==0]
    header=struct.pack('<QI',ns,len(ds))+b''.join(struct.pack('<5f',*d['box'],d['conf']) for d in ds)
    for proc in procs:proc.stdin.write(header);proc.stdin.write(pix)
   assert not dec.stdout.read(1);assert dec.wait()==0
  for proc in procs:proc.stdin.close()
  for proc in procs:assert proc.wait()==0
  for log in logs:log.close()
  for name in order:
   data=list(csv.DictReader((out/f'{clip}-{round}-{name}.csv').open()));times=sorted(float(r['ms']) for r in data[10:])
   summary.append(dict(clip=clip,round=round,build=name,frames=len(data),mean_ms=statistics.mean(times),p95_ms=times[int(.95*len(times))]))
  print(clip,'round',round,'done',flush=True)
(out/'summary.json').write_text(json.dumps(summary,indent=2))
