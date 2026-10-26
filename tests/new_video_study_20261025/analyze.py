"""Rank diagnostic candidates; these metrics are not visual ground truth."""
import collections,json,math,statistics
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'runs/new-video-study-20261025'
events=[];summaries=[]
for clip in json.loads((out/'selected.json').read_text(encoding='utf-8')):
 p=out/clip['id']
 if not (p/'complete.json').exists():continue
 rows=[json.loads(s) for s in (p/'vision.jsonl').read_text().splitlines()]
 local=[];streak=None;holds=[]
 for i,r in enumerate(rows):
  if r['source']=='cue_hold':
   if streak is None:streak=i
  elif streak is not None:holds.append((streak,i-1));streak=None
  if i==0:continue
  prev=rows[i-1]
  if r['has_target'] and prev['has_target'] and r['generation']==prev['generation']:
   jump=math.dist(r['target'],prev['target'])
   cue_same=r['motion_cue'][0] and r['motion_cue'][0]==prev['motion_cue'][0]
   cue_step=math.dist(r['motion_cue'][1:],prev['motion_cue'][1:]) if cue_same else None
   residual=math.hypot(*[r['target'][k]-prev['target'][k]-(r['motion_cue'][k+1]-prev['motion_cue'][k+1]) for k in (0,1)]) if cue_same else None
   if residual is not None and residual>18:
    local.append(dict(kind='cue_relative_jump',frame=i,t=r['t'],score=residual,jump=jump,cue_step=cue_step,source=r['source'],previous=prev['source']))
   elif jump>50:
    local.append(dict(kind='same_generation_jump',frame=i,t=r['t'],score=jump,source=r['source'],previous=prev['source']))
  if r['cue_reason']=='cue_preserves_body_extent' and r['detections']:
   # Find a plausible current body beneath the active cue, then measure whether
   # the retained point is outside its current extent. Human inspection required.
   cx,cy=r['motion_cue'][1:]
   ds=[d for d in r['detections'] if d['conf']>=.4 and not d['friendly'] and d['box'][0]-15<=cx<=d['box'][2]+15]
   if ds:
    d=min(ds,key=lambda d:abs((d['box'][0]+d['box'][2])*.5-cx));x1,y1,x2,y2=d['box'];x,y=r['target']
    outside=math.hypot(max(x1-x,0,x-x2),max(y1-y,0,y-y2))
    if outside>10:local.append(dict(kind='retained_point_outside_current_box',frame=i,t=r['t'],score=outside,source=r['source'],box=d['box'],conf=d['conf']))
 if streak is not None:holds.append((streak,len(rows)-1))
 for first,last in holds:
  if rows[last]['t']-rows[first]['t']>=.3:
   local.append(dict(kind='long_cue_hold',frame=first,t=rows[first]['t'],score=rows[last]['t']-rows[first]['t'],last=last,end=rows[last]['t']))
 # Keep the strongest independent events per category, with 200ms separation.
 picks=[]
 for kind in sorted({r['kind'] for r in local}):
  chosen=[]
  for e in sorted((r for r in local if r['kind']==kind),key=lambda r:r['score'],reverse=True):
   if all(abs(e['t']-v['t'])>.2 for v in chosen):chosen.append(e)
   if len(chosen)>=12:break
  picks+=chosen
 for e in picks:e['clip']=clip['id'];events.append(e)
 stages=[r['selector_ms'] for r in rows[10:]]
 summary={'clip':clip['id'],'name':Path(clip['path']).name,'frames':len(rows),'duration':rows[-1]['t'],
  'targets':sum(r['has_target'] for r in rows),'sources':dict(collections.Counter(r['source'] for r in rows)),
  'protected':sum(r['cue_reason']=='cue_preserves_body_extent' for r in rows),'generation_max':max(r['generation'] for r in rows),
  'friendly_boxes':sum(d['friendly'] for r in rows for d in r['detections']),
  'selector_mean_ms':statistics.mean(stages),'selector_p95_ms':sorted(stages)[int(len(stages)*.95)],
  'events':dict(collections.Counter(e['kind'] for e in local))}
 summaries.append(summary)
(out/'summary.json').write_text(json.dumps(summaries,indent=2,ensure_ascii=False),encoding='utf-8')
(out/'candidates.json').write_text(json.dumps(events,indent=2),encoding='utf-8')
for r in summaries:print(r['clip'],r['frames'],r['sources'],'events',r['events'])
print('ranked candidates',len(events))
