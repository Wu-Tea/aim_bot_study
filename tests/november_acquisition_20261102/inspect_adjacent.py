"""Contact sheet for changed friendly-veto decisions; original recorded boxes."""
import csv,json,subprocess
from pathlib import Path
import cv2,numpy as np
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'runs/incident-20261102/fix/adjacent'
clips=json.loads((ROOT/'runs/new-video-study-20261025/selected.json').read_text())+json.loads((ROOT/'runs/cue-motion-20261025/selected.json').read_text())
tiles=[]
for name,indices in [('v07',[21017,21030]),('q1',[1559,6020,6728]),('q2',[189,202,209])]:
 clip=next(c for c in clips if c['id']==name);hdr=name.startswith('q')
 base=ROOT/('runs/cue-motion-20261025/before-hdr' if hdr else 'runs/new-video-fix-20261025/final')/name
 records=[json.loads(s) for s in (base/'vision.jsonl').read_text().splitlines()]
 after=list(csv.DictReader((OUT/f'{name}-after.csv').open()))
 for idx in indices:
  vf=f'select=eq(n\\,{idx}),crop=640:512'
  if hdr:vf+=',zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=mobius:desat=0,zscale=t=bt709:m=bt709:r=full'
  data=subprocess.check_output(['ffmpeg','-v','error','-i',clip['path'],'-vf',vf,'-frames:v','1','-f','rawvideo','-pix_fmt','bgr24','pipe:1'])
  frame=np.frombuffer(data,dtype=np.uint8).reshape(512,640,3).copy()
  for d in records[idx]['detections']:
   x1,y1,x2,y2=map(round,d['box']);cv2.rectangle(frame,(x1,y1),(x2,y2),(0,220,255),1)
  x,y=round(float(after[idx]['x'])),round(float(after[idx]['y']));cv2.drawMarker(frame,(x,y),(0,255,0),cv2.MARKER_CROSS,12,1)
  cv2.putText(frame,f'{name} frame {idx}',(15,30),cv2.FONT_HERSHEY_SIMPLEX,.7,(255,255,255),2)
  tiles.append(frame)
cv2.imwrite(str(OUT/'friendly-review.jpg'),np.vstack([np.hstack(tiles[i:i+2]) for i in range(0,len(tiles),2)]))
