"""Original logged detections plus nearest SDR video frame; never live output."""
import argparse,json,pickle,struct,subprocess,csv,statistics
from pathlib import Path
import cv2
ROOT=Path(__file__).resolve().parents[2];R=ROOT/'runs/incident-20261102'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=R/'fix')
parser.add_argument('versions',nargs='*',default=['before','after'])
args=parser.parse_args();O=args.output;O.mkdir(parents=True,exist_ok=True)
M=json.loads((R/'manifest.json').read_text(encoding='utf-8'));D=pickle.load((R/'index.pkl').open('rb'))
windows=[('E15',96.8,99.4),('E17',105.8,107.8),('E27',183.5,185.4),('E76',545.8,548.2),('E41',284.8,286.3),('E36',257.5,260.2),('E04',35.8,37.5)]
versions=args.versions or ['before','after'];summary=[]
for name,a,b in windows:
 rows=[r for r in D['selection'] if a<=r['_video_s']-.1375<=b]
 rows.sort(key=lambda r:r['vision_capture_ns'])
 cap=cv2.VideoCapture(M['source_video']);last=-1;crop=None;procs=[];logs=[]
 for version in versions:
  log=(O/f'{name}-{version}.txt').open('wb');logs.append(log)
  procs.append(subprocess.Popen([str(O/f'build/Release/replay_{version}.exe'),str(O/f'{name}-{version}.csv')],stdin=subprocess.PIPE,stdout=log,stderr=log))
 stamps=[]
 for row in rows:
  t=row['_video_s']-.1375-(row['output_sent_ns']-row['vision_capture_ns'])/1e9;idx=round(t*120)
  if idx!=last:
   if idx!=last+1:cap.set(cv2.CAP_PROP_POS_FRAMES,idx)
   ok,f=cap.read();assert ok
   crop=cv2.cvtColor(f[284:796,640:1280],cv2.COLOR_BGR2BGRA).tobytes();last=idx
  boxes=row['selection']['boxes'];header=struct.pack('<QI',row['vision_capture_ns'],len(boxes))+b''.join(struct.pack('<5f',*x['box'],x['confidence']) for x in boxes)
  for p in procs:p.stdin.write(header);p.stdin.write(crop)
  stamps.append(dict(video_s=t,frame_id=row['frame_id'],file=row['_file'],line=row['_line']))
 for p in procs:p.stdin.close()
 for p in procs:assert p.wait()==0
 for log in logs:log.close()
 cap.release();(O/f'{name}-frames.json').write_text(json.dumps(stamps),encoding='utf-8')
 for version in versions:
  records=list(csv.DictReader((O/f'{name}-{version}.csv').open()));assert len(records)==len(rows)
  times=sorted(float(r['ms']) for r in records[10:]);stat=dict(case=name,version=version,frames=len(records),target=sum(r['has_target']=='1' for r in records),friendly=sum(int(r['friendly']) for r in records),geometry=sum(int(r['geometry']) for r in records),median_ms=statistics.median(times),mean_ms=statistics.mean(times),p95_ms=times[int(.95*len(times))])
  summary.append(stat);print(json.dumps(stat),flush=True)
(O/'replay_summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
