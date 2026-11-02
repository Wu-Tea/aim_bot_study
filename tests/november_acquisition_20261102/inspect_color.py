import json,cv2,numpy as np,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
R=Path(__file__).resolve().parents[2]/'runs/incident-20261102';O=R/'fix'
M=json.loads((R/'manifest.json').read_text(encoding='utf-8'));rows=[json.loads(s) for s in (R/'loss-search/user_E27_records.jsonl').read_text(encoding='utf-8').splitlines()]
cap=cv2.VideoCapture(M['source_video']);montage=Image.new('RGB',(1280,2*560));draw=ImageDraw.Draw(montage)
for i,v in enumerate([185.0912,185.2324,185.2382,185.2438]):
 row=min([r for r in rows if r['type']=='selection_decision' and r['selection']['boxes']],key=lambda r:abs(r['video_s']-(r['output_sent_ns']-r['vision_capture_ns'])/1e9-v))
 t=row['video_s']-(row['output_sent_ns']-row['vision_capture_ns'])/1e9;cap.set(cv2.CAP_PROP_POS_FRAMES,round(t*120));ok,frame=cap.read();crop=frame[284:796,640:1280];bgra=cv2.cvtColor(crop,cv2.COLOR_BGR2BGRA);p=O/f'color_{i}.bgra';p.write_bytes(bgra.tobytes())
 x,y,x2,y2=row['selection']['boxes'][0]['box'];w=x2-x;h=y2-y;rh=int(max(12,min(36,h*.2)));rw=int(max(24,min(80,w*.8)));cx=(x+x2)/2
 bounds=[max(0,int(cx-rw/2)),max(0,int(y)-2-rh),min(640,int(cx+rw/2)),max(0,int(y)-2)]
 l,top,r,b=bounds;patch=[max(0,l-19),max(0,top-19),min(640,r+19),min(512,b+19)]
 text=subprocess.check_output([str(O/'build/Release/color_probe.exe'),str(p),*map(str,patch)],encoding='utf-8');print(t,bounds,text.strip())
 im=Image.fromarray(cv2.cvtColor(crop,cv2.COLOR_BGR2RGB));d=ImageDraw.Draw(im);d.rectangle(bounds,outline='yellow')
 for line in text.splitlines():
  xx,yy,size,score=map(float,line.split(','));d.ellipse((xx-size,yy-size,xx+size,yy+size),outline='red',width=2);d.text((xx,yy+size),f'{score:.2f}',fill='red')
 montage.paste(im,(i%2*640,i//2*560));draw.text((i%2*640+5,i//2*560+518),f'{t:.4f}s',fill='white')
montage.save(O/'friendly_components.png');cap.release()
