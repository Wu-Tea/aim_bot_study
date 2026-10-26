"""Feed identical recorded detections and original decoded pixels to multiple builds."""
import argparse,json,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('video');p.add_argument('review');p.add_argument('out');p.add_argument('executables',nargs='+')
p.add_argument('--legacy-colors',action='store_true',help='synthetic HSV yellow/green -> magenta/cyan; outside timed region')
p.add_argument('--uniform-fps',type=float,help='explicit clock control matching legacy GPU review; default actual PTS')
a=p.parse_args()
if a.legacy_colors:
 import cv2,numpy as np
out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
rows=[json.loads(s) for s in Path(a.review).read_text(encoding='utf-8').splitlines()]
probe=subprocess.run(['ffprobe','-v','error','-select_streams','v','-show_entries','frame=best_effort_timestamp_time','-of','json',a.video],capture_output=True,check=True)
pts=[float(f['best_effort_timestamp_time']) for f in json.loads(probe.stdout)['frames']]
assert len(rows)==len(pts)
if a.uniform_fps:
 assert a.uniform_fps>0
 pts=[i/a.uniform_fps for i in range(len(rows))]
logs=[];runs=[]
for i,exe in enumerate(a.executables):
 log=(out/f'{i}-native.txt').open('wb');logs.append(log)
 runs.append(subprocess.Popen([exe,str(out/f'{i}.csv')],stdin=subprocess.PIPE,stdout=log,stderr=log))
with (out/'decode.txt').open('wb') as log:
 dec=subprocess.Popen(['ffmpeg','-v','error','-i',a.video,'-vf','crop=640:512','-fps_mode','passthrough','-f','rawvideo','-pix_fmt','bgra','pipe:1'],stdout=subprocess.PIPE,stderr=log)
 for r,t in zip(rows,pts):
  pix=dec.stdout.read(640*512*4);assert len(pix)==640*512*4
  if a.legacy_colors:
   image=np.frombuffer(pix,dtype=np.uint8).reshape(512,640,4).copy()
   hsv=cv2.cvtColor(image[:,:,:3],cv2.COLOR_BGR2HSV);h,s,v=cv2.split(hsv)
   enemy=(h>=20)&(h<=35)&(s>=120)&(v>=120)
   friend=(h>=40)&(h<=85)&(s>=80)&(v>=50)
   image[enemy,:3]=[255,0,255];image[friend,:3]=[255,255,0]
   pix=image.tobytes()
  ds=[d for d in r['detections'] if d['class']==0]
  header=struct.pack('<QI',round(1e9+(t-pts[0])*1e9),len(ds))+b''.join(struct.pack('<5f',*d['box'],d['conf']) for d in ds)
  for proc in runs:proc.stdin.write(header);proc.stdin.write(pix)
 assert not dec.stdout.read(1);assert dec.wait()==0
for proc in runs:proc.stdin.close()
for proc in runs:assert proc.wait()==0
for log in logs:log.close()
print('frames',len(rows))
