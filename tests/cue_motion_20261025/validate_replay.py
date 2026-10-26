"""Fixed detections, identical decoded pixels: behavior regression and stage timings."""
import csv,json,statistics,struct,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2];study=root/'runs/cue-motion-20261025';out=study/'validation-release';out.mkdir(exist_ok=True)
old=root/'runs/new-video-study-20261025';old_final=root/'runs/new-video-fix-20261025/final'
new_clips=json.loads((study/'selected.json').read_text());old_clips=json.loads((old/'selected.json').read_text())
exes={'before':study/'before/replay.exe','after':root/'runs/ironsight-cue-drift-20261025/build/Release/replay.exe'}
summary=[];changes=[]
for iteration,clips in [(0,old_clips),(0,new_clips),(1,new_clips),(2,new_clips)]:
 for clip in clips:
  q=clip['id'];new=q.startswith('q');base=study/'before-hdr' if new else old_final
  data=[json.loads(s) for s in (base/q/'vision.jsonl').read_text().splitlines()]
  stamps=list(map(int,(base/q/'timestamps.txt').read_text().split()))
  vf='crop=640:512'
  if new:vf+=',zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=mobius:desat=0,zscale=t=bt709:m=bt709:r=full,format=bgra'
  order=['before','after'] if iteration%2==0 else ['after','before'];procs=[];logs=[]
  for name in order:
   log=(out/f'{q}-{iteration}-{name}.txt').open('wb');logs.append(log)
   procs.append(subprocess.Popen([str(exes[name]),str(out/f'{q}-{iteration}-{name}.csv')],stdin=subprocess.PIPE,stdout=log,stderr=log))
  with (out/f'{q}-{iteration}-decode.txt').open('wb') as log:
   dec=subprocess.Popen(['ffmpeg','-v','error','-i',clip['path'],'-vf',vf,'-fps_mode','passthrough','-f','rawvideo','-pix_fmt','bgra','pipe:1'],stdout=subprocess.PIPE,stderr=log)
   for row,ns in zip(data,stamps):
    pix=dec.stdout.read(640*512*4);assert len(pix)==640*512*4
    ds=[d for d in row['detections'] if d['class']==0]
    header=struct.pack('<QI',ns,len(ds))+b''.join(struct.pack('<5f',*d['box'],d['conf']) for d in ds)
    for proc in procs:proc.stdin.write(header);proc.stdin.write(pix)
   assert not dec.stdout.read(1);assert dec.wait()==0
  for proc in procs:proc.stdin.close()
  for proc in procs:assert proc.wait()==0
  for log in logs:log.close()
  csvs={}
  for name in order:
   rows=list(csv.DictReader((out/f'{q}-{iteration}-{name}.csv').open()));csvs[name]=rows
   assert len(rows)==len(data)
   times=sorted(float(r['ms']) for r in rows[10:])
   summary.append(dict(clip=q,round=iteration,build=name,frames=len(rows),mean_ms=statistics.mean(times),p95_ms=times[int(.95*len(times))]))
  # Saved before binary must reproduce the exact baseline used for comparison.
  for a,b in zip(data,csvs['before']):
   assert a['has_target']==bool(int(b['target'])) and a['generation']==int(b['generation']) and a['source']==b['source']
   assert abs(a['target'][0]-float(b['x']))<1e-4 and abs(a['target'][1]-float(b['y']))<1e-4
  if iteration==0:
   changes.append(dict(clip=q,lost=[i for i,(a,b) in enumerate(zip(csvs['before'],csvs['after'])) if a['target']=='1' and b['target']=='0'],
    gained=[i for i,(a,b) in enumerate(zip(csvs['before'],csvs['after'])) if a['target']=='0' and b['target']=='1']))
  print(q,iteration,'done',flush=True)
  (out/'summary.json').write_text(json.dumps(summary,indent=2));(out/'changes.json').write_text(json.dumps(changes,indent=2))
