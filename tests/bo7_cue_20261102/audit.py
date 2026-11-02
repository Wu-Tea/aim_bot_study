"""Real screenshot and explicitly synthetic health-bar boundary probes; no live I/O."""
import csv,json,struct,subprocess,sys
from pathlib import Path
import cv2,numpy as np
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'runs/bo7-cue-20261102';OUT.mkdir(exist_ok=True)
original=cv2.imread(sys.argv[1]);assert original.shape[:2]==(420,414)
frame=np.zeros((512,640,4),np.uint8);frame[47:467,113:527]=cv2.cvtColor(original,cv2.COLOR_BGR2BGRA)
bar=frame[146:160,209:370].copy();dot=frame[160:175,282:297].copy()
records=[];examples={}
def scan(name,pixels,kind):
 path=OUT/'probe.bgra';path.write_bytes(pixels.tobytes())
 result=subprocess.check_output([str(OUT/'build/Release/cue_probe.exe'),str(path)],text=True).strip().split(';')
 cues=[list(map(float,c.split(','))) for c in result[1:]]
 records.append(dict(name=name,kind=kind,cues=cues))
 if cues and kind!='positive':examples[name]=pixels.copy()
 return cues
scan('original',frame,'positive');examples['original']=frame
without=frame.copy();without[160:175,282:297]=0
scan('original_without_round_cue',without,'negative');examples['original_without_round_cue']=without
for scale in [.5,.75,1,1.25,1.5,2]:
 patch=cv2.resize(bar,None,fx=scale,fy=scale,interpolation=cv2.INTER_LINEAR);f=np.zeros_like(frame)
 h,w=patch.shape[:2];f[140:140+h,320-w//2:320-w//2+w]=patch
 scan(f'bar_scale_{scale}',f,'synthetic_negative')
for width in range(4,33):
 for style in ['remaining_fill','joined_caps','interior_fragment']:
  if style=='joined_caps':
   left=width//2;patch=np.concatenate([bar[:,:left],bar[:,-(width-left):]],axis=1)
  elif style=='remaining_fill':patch=bar[:,:width]
  else:patch=bar[:,75:75+width]
  # Remaining fill/caps retain the real bar's left anchor. Interior crops
  # retain their source position, directly above the real round cue.
  left_x=284 if style=='interior_fragment' else 209
  f=np.zeros_like(frame);f[146:160,left_x:left_x+width]=patch
  scan(f'{style}_{width}',f,'synthetic_negative')
# Dot and bar stay separate, touch, or overlap. A merge can suppress a real cue;
# these are morphology stress inputs, not assertions about BO7's animation.
blue,green,red=[dot[:,:,i].astype(np.int32) for i in range(3)]
hi=np.maximum(red,blue);delta=hi-green
dot_mask=(green<red)&(green<blue)&(hi>=120)&(255*delta>=120*hi)&(15*np.abs(red-blue)<=4*delta)
for gap in [0,1,2,3,4,6,8,12,14]:
 f=np.zeros_like(frame);f[146:160,209:370]=bar
 region=f[146+gap:161+gap,282:297];region[dot_mask]=dot[dot_mask]
 scan(f'bar_dot_offset_{gap}',f,'synthetic_merge')
def chain(name,end,person_last=False,version='after'):
 output=OUT/f'{name}.csv'
 proc=subprocess.Popen([str(ROOT/f'runs/incident-20261102/fix/build/Release/replay_{version}.exe'),str(output)],stdin=subprocess.PIPE)
 body=[276.,181.,325.,255.,.9] # Manually estimated body, not model inference.
 for i in range(6):
  pixels=frame if i<2 else end;boxes=[body] if i<2 or person_last else []
  proc.stdin.write(struct.pack('<QI',1_000_000_000+i*10_000_000,len(boxes)))
  for box in boxes:proc.stdin.write(struct.pack('<5f',*box))
  proc.stdin.write(pixels.tobytes())
 proc.stdin.close();assert proc.wait()==0
 return list(csv.DictReader(output.open()))
chain_results={'full_bar_after_cue_loss':chain('full_bar_after_cue_loss',without)}
accepted=[r for r in records if r['kind']=='synthetic_negative' and r['cues']]
if accepted:
 r=next(r for r in accepted if r['name'].startswith('interior_fragment'))
 p=examples[r['name']];chain_results['fragment_after_cue_loss']=chain('fragment_after_cue_loss',p)
 chain_results['baseline_fragment_after_cue_loss']=chain('baseline_fragment_after_cue_loss',p,version='before')
 with_dot=p.copy();with_dot[160:175,282:297]=dot
 chain_results['fragment_and_real_cue']=chain('fragment_and_real_cue',with_dot)
result=dict(screenshot=sys.argv[1],padding=[113,47],records=records,chains=chain_results,
 limitations=['Short bars are synthetic crops/cap joins, not recorded BO7 low-health frames.','Body box in lifecycle check is manually estimated.','No model inference or production changes.'])
(OUT/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('original cues',records[0]['cues']);print('bar only',records[1]['cues'])
print('synthetic bar false candidates',[(r['name'],r['cues']) for r in accepted])
print('merge counts',[(r['name'],len(r['cues'])) for r in records if r['kind']=='synthetic_merge'])
print('chains',{k:[(r['has_target'],r['source'],r['reason']) for r in rows] for k,rows in chain_results.items()})
for name in ['original','original_without_round_cue']+[r['name'] for r in accepted[:4]]:
 cv2.imwrite(str(OUT/f'{name}.png'),examples[name])
annotated=original.copy()
cv2.rectangle(annotated,(96,99),(257,113),(0,165,255),1)
cv2.circle(annotated,(176,120),7,(0,255,0),1)
cv2.putText(annotated,'bar rejected',(98,94),cv2.FONT_HERSHEY_SIMPLEX,.45,(0,165,255),1)
cv2.putText(annotated,'cue',(187,124),cv2.FONT_HERSHEY_SIMPLEX,.4,(0,255,0),1)
cv2.imwrite(str(OUT/'verified.png'),annotated)
