"""Adjacent recorded materials. Fixed boxes, same decoded pixels, no live I/O."""
import argparse,csv,json,struct,subprocess,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'runs/incident-20261102/fix')
parser.add_argument('--cases',nargs='+',default=['v01','v06','v07','q1','q2'])
args=parser.parse_args();R=args.output;O=R/'adjacent';O.mkdir(parents=True,exist_ok=True)
clips=json.loads((ROOT/'runs/new-video-study-20261025/selected.json').read_text())+json.loads((ROOT/'runs/cue-motion-20261025/selected.json').read_text())
summary=[]
for clip in [c for c in clips if c['id'] in args.cases]:
 name=clip['id'];hdr=name.startswith('q');base=ROOT/('runs/cue-motion-20261025/before-hdr' if hdr else 'runs/new-video-fix-20261025/final')/name
 rows=[json.loads(s) for s in (base/'vision.jsonl').read_text().splitlines()];stamps=list(map(int,(base/'timestamps.txt').read_text().split()))
 vf='crop=640:512'
 if hdr:vf+=',zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=mobius:desat=0,zscale=t=bt709:m=bt709:r=full,format=bgra'
 logs=[];procs=[]
 for version in ['before','after']:
  log=(O/f'{name}-{version}.txt').open('wb');logs.append(log);procs.append(subprocess.Popen([str(R/f'build/Release/replay_{version}.exe'),str(O/f'{name}-{version}.csv')],stdin=subprocess.PIPE,stdout=log,stderr=log))
 with (O/f'{name}-decode.txt').open('wb') as log:
  decoder=subprocess.Popen(['ffmpeg','-v','error','-i',clip['path'],'-vf',vf,'-fps_mode','passthrough','-f','rawvideo','-pix_fmt','bgra','pipe:1'],stdout=subprocess.PIPE,stderr=log)
  for row,ns in zip(rows,stamps):
   pixels=decoder.stdout.read(640*512*4);assert len(pixels)==640*512*4
   ds=[d for d in row['detections'] if d['class']==0];header=struct.pack('<QI',ns,len(ds))+b''.join(struct.pack('<5f',*d['box'],d['conf']) for d in ds)
   for proc in procs:proc.stdin.write(header);proc.stdin.write(pixels)
  assert not decoder.stdout.read(1);assert decoder.wait()==0
 for proc in procs:proc.stdin.close()
 for proc in procs:assert proc.wait()==0
 for log in logs:log.close()
 before=list(csv.DictReader((O/f'{name}-before.csv').open()));after=list(csv.DictReader((O/f'{name}-after.csv').open()));assert len(before)==len(after)==len(rows)
 result=dict(case=name,frames=len(rows),gained=[i for i,(b,a) in enumerate(zip(before,after)) if b['has_target']=='0' and a['has_target']=='1'],lost=[i for i,(b,a) in enumerate(zip(before,after)) if b['has_target']=='1' and a['has_target']=='0'])
 for version,records in [('before',before),('after',after)]:
  times=sorted(float(r['ms']) for r in records[10:]);result[version]=dict(target=sum(r['has_target']=='1' for r in records),mean_ms=statistics.mean(times),p95_ms=times[int(.95*len(times))])
 summary.append(result);(O/'summary.json').write_text(json.dumps(summary,indent=2));print(name,'frames',len(rows),'gained',len(result['gained']),'lost',len(result['lost']),flush=True)
