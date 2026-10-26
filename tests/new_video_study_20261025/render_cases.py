import argparse,json,cv2
from pathlib import Path
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('cases',nargs='+',help='v01:1459');p.add_argument('--results-root',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2];baseline=root/'runs/new-video-study-20261025';out=a.results_root or baseline
clips={r['id']:r for r in json.loads((baseline/'selected.json').read_text(encoding='utf-8'))}
for spec in a.cases:
 name,n=spec.split(':');n=int(n);rows=[json.loads(s) for s in (out/name/'vision.jsonl').read_text().splitlines()]
 cap=cv2.VideoCapture(clips[name]['path']);sheet=Image.new('RGB',(1920,550))
 for k,f in enumerate([max(0,n-1),n,min(len(rows)-1,n+1)]):
  cap.set(cv2.CAP_PROP_POS_FRAMES,f);ok,bgr=cap.read();assert ok
  y=(bgr.shape[0]-512)//2;x=(bgr.shape[1]-640)//2
  im=Image.fromarray(cv2.cvtColor(bgr[y:y+512,x:x+640],cv2.COLOR_BGR2RGB));d=ImageDraw.Draw(im);r=rows[f]
  for j,box in enumerate(r['detections']):
   color='orange' if j==r['selected'] else 'cyan' if box['friendly'] else 'lime'
   d.rectangle(box['box'],outline=color,width=2);d.text(box['box'][:2],f'{j}: {box["conf"]:.2f}',fill=color)
  for c in r['independent_cues']:
   cx,cy=c['xy'];d.rectangle((cx-5,cy-5,cx+5,cy+5),outline='magenta',width=1)
  if r['has_target']:
   tx,ty=r['target'];d.line((tx-8,ty,tx+8,ty),fill='red',width=2);d.line((tx,ty-8,tx,ty+8),fill='red',width=2)
   d.rectangle(r['body'],outline='white',width=1)
  sheet.paste(im,(640*k,0));ImageDraw.Draw(sheet).text((640*k+5,516),f'{name} f{f} {r["t"]:.3f}s gen{r["generation"]} {r["source"]}\n{r["cue_reason"]}',fill='white')
 cap.release();sheet.save(out/f'{name}-{n}.png')
 print(name,n)
