"""Compare full native exports. Counts are observations, not accuracy scores."""
import collections,json,math
from pathlib import Path
root=Path(__file__).resolve().parents[2];before=root/'runs/new-video-study-20261025';out=root/'runs/new-video-fix-20261025';after=out/'final'
clips=json.loads((before/'selected.json').read_text(encoding='utf-8'));pairs={};coverage=[]
for clip in clips:
 c=clip['id'];a=[json.loads(s) for s in (before/c/'vision.jsonl').read_text().splitlines()];b=[json.loads(s) for s in (after/c/'vision.jsonl').read_text().splitlines()]
 assert len(a)==len(b)
 assert all(x['frame']==y['frame'] and x['t']==y['t'] for x,y in zip(a,b))
 # Only compare model outputs; colors, correspondence and selection may change.
 assert all([(d['box'],d['conf'],d['class']) for d in x['detections']]==[(d['box'],d['conf'],d['class']) for d in y['detections']] for x,y in zip(a,b))
 pairs[c]=(a,b)
 coverage.append(dict(clip=c,frames=len(a),target_before=sum(r['has_target'] for r in a),target_after=sum(r['has_target'] for r in b),
  lost=sum(x['has_target'] and not y['has_target'] for x,y in zip(a,b)),gained=sum(not x['has_target'] and y['has_target'] for x,y in zip(a,b)),
  sources_after=dict(collections.Counter(r['source'] for r in b))))
cases=[]
for clip,n,label in [('v01',1459,'friend_overlap'),('v07',1864,'statue_takeover'),('v07',2817,'old_reference_return'),('v07',5332,'head_fragment'),('v07',10611,'background_takeover'),('v07',21803,'gradual_top_drift'),('v07',29133,'small_side_object'),('v08',2252,'edge_offset')]:
 case={'name':label,'clip':clip,'frame':n}
 for tag,rows in zip(['before','after'],pairs[clip]):
  r=rows[n];prev=rows[n-1]
  case[tag]={k:r[k] for k in ['t','has_target','target','body','source','generation','motion_cue','cue_reason']}
  case[tag]['jump_px']=math.dist(r['target'],prev['target']) if r['has_target'] and prev['has_target'] else None
 cases.append(case)
checks={}
a,b=pairs['v01'];checks['friend_f1454_f1459_all_excluded']=all(r['detections'][0]['friendly'] for r in b[1454:1460])
a,b=pairs['v07'];checks['original_body_returns_f21803']=b[21803]['source']=='observed'
checks['brief_gap_edge_person_f22115_f22146']=dict(before=sum(r['has_target'] for r in a[22115:22147]),after=sum(r['has_target'] for r in b[22115:22147]))
a,b=pairs['v08'];checks['no_inflated_edge_reference_f2247_f2251']=all(r['cue_reason']!='person_cue_reference_recorded' for r in b[2247:2252])
checks['no_bad_edge_hold_f2251']=not b[2251]['has_target']
checks['old_ironsight_control_offsets']=[dict(frame=i,before=a[i]['target'][1]-a[i]['motion_cue'][2],after=b[i]['target'][1]-b[i]['motion_cue'][2],source=b[i]['source']) for i in [4828,5296] if a[i]['motion_cue'][0] and b[i]['motion_cue'][0]]
checks['side_pose_occlusion_f3637_f3650']=dict(before=sum(r['has_target'] for r in a[3637:3651]),after=sum(r['has_target'] for r in b[3637:3651]))
summary=dict(coverage=coverage,cases=cases,checks=checks)
(out/'behavior-final.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(checks,indent=2))
for c in cases:print(c['name'],c['before']['jump_px'],'->',c['after']['jump_px'],c['after']['source'])
print('verified identical raw model detections',sum(c['frames'] for c in coverage),'frames')
