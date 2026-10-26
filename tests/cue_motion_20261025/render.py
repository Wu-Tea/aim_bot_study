import argparse,json,subprocess,io
from pathlib import Path
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('cases',nargs='+');p.add_argument('--tag',default='before-hdr');a=p.parse_args()
root=Path(__file__).resolve().parents[2]/'runs/cue-motion-20261025'
clips={r['id']:r for r in json.loads((root/'selected.json').read_text())}
vf='crop=640:512,zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=mobius:desat=0,zscale=t=bt709:m=bt709:r=full,format=rgb24'
for spec in a.cases:
 q,n=spec.split(':');n=int(n);rows=[json.loads(s) for s in (root/a.tag/q/'vision.jsonl').read_text().splitlines()]
 sheet=Image.new('RGB',(1920,550))
 for k,f in enumerate([max(0,n-1),n,min(len(rows)-1,n+1)]):
  r=rows[f];raw=subprocess.run(['ffmpeg','-v','error','-ss',str(r['t']),'-i',clips[q]['path'],'-vf',vf,'-frames:v','1','-f','rawvideo','-pix_fmt','rgb24','pipe:1'],capture_output=True,check=True).stdout
  im=Image.frombytes('RGB',(640,512),raw);d=ImageDraw.Draw(im)
  for j,box in enumerate(r['detections']):
   color='orange' if j==r['selected'] else 'cyan' if box['friendly'] else 'lime'
   d.rectangle(box['box'],outline=color,width=2);d.text(box['box'][:2],f'{j}:{box["conf"]:.2f}',fill=color)
  for c in r['independent_cues']:
   cx,cy=c['xy'];d.rectangle((cx-5,cy-5,cx+5,cy+5),outline='magenta')
  if r['has_target']:
   x,y=r['target'];d.line((x-8,y,x+8,y),fill='red',width=2);d.line((x,y-8,x,y+8),fill='red',width=2);d.rectangle(r['body'],outline='white')
  sheet.paste(im,(640*k,0));ImageDraw.Draw(sheet).text((640*k+5,516),f'{q} f{f} {r["t"]:.3f}s gen{r["generation"]} {r["source"]}\n{r["cue_reason"]}',fill='white')
 sheet.save(root/a.tag/f'{q}-{n}.png');print(spec)
