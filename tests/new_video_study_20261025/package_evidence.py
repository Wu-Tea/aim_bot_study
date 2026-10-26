"""Save bounded native trajectories and reproducible diagnostic measurements."""
import csv,json,math,statistics
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'runs/new-video-study-20261025'
dest=Path(__file__).resolve().parent/'fixtures';dest.mkdir(exist_ok=True)
videos={c['id']:c for c in json.loads((out/'selected.json').read_text(encoding='utf-8'))}
cache={c:[json.loads(s) for s in (out/c/'vision.jsonl').read_text().splitlines()] for c in videos}
index=[];portable=[]
for line in (out/'closed-manifest.txt').read_text().splitlines():
 name,path,start=line.split();start=float(start);clip=Path(path).parent.name
 records=Path(path).read_text().splitlines();rows=cache[clip]
 selected=[i for i,r in enumerate(rows) if start<=r['t']<start+2]
 ns=int(records[selected[0]].split()[1]);packed=[]
 for ordinal,i in enumerate(selected,1):
  fields=records[i].split();fields[0]=str(ordinal);fields[1]=str(int(fields[1])-ns+1000000000)
  packed.append(' '.join(fields))
 (dest/f'{name}.records').write_text('\n'.join(packed)+'\n')
 portable.append(f'{name} tests/new_video_study_20261025/fixtures/{name}.records 0\n')
 index.append(dict(name=name,clip=clip,video=videos[clip]['path'],first_frame=selected[0],last_frame=selected[-1],first_video_t=rows[selected[0]]['t'],frames=len(selected),timestamp_rebased=True,
  interpretation='Recorded screen observations, not world-space target truth. Controller input is synthetic.'))
(dest/'manifest.txt').write_text(''.join(portable))
(dest/'index.json').write_text(json.dumps(index,ensure_ascii=False,indent=2),encoding='utf-8')

def cost(c,d):
 x,y=c;b=d['box'];w=b[2]-b[0];h=b[3]-b[1]
 if not (b[0]-.2*w<=x<=b[2]+.2*w and b[1]-max(36,.6*h)<=y<=b[1]+.4*h):return None
 return .55*abs(x-(b[0]+b[2])*.5)/max(16,.65*w)+.35*abs(y-(b[1]-.08*h))/max(24,.45*h)
r=cache['v01'][1458];c=r['motion_cue'][1:]
evidence={'friend_overlap':{'frame':1458,'cue':c,'association_costs':[cost(c,d) for d in r['detections']],
 'previous_target':r['target'],'next_target':cache['v01'][1459]['target'],
 'jump_px':math.dist(r['target'],cache['v01'][1459]['target'])},'snapshots':[]}
for clip,n in [('v01',1458),('v01',1459),('v06',650),('v06',679),('v07',1864),('v07',5332),('v07',10611),('v07',21710),('v07',21759),('v07',21803),('v08',2247),('v08',2251),('v08',2252)]:
 evidence['snapshots'].append({'clip':clip,**cache[clip][n]})
(dest/'diagnostic-snapshots.json').write_text(json.dumps(evidence,indent=2))
traces=list(csv.DictReader((out/'open-20261025/trace.csv').open()))
effects=[]
for cid,a,b,label in [(0,12.13,12.25,'friend_to_enemy'),(15,5.408,5.68,'reload_weapon'),(18,44.42,44.45,'head_fragment'),(18,88.43,88.53,'background_takeover'),(21,18.77,18.80,'clipped_offset')]:
 rs=[r for r in traces if int(r['case'])==cid and a<=float(r['video_t'])<=b]
 effects.append(dict(case=label,interval=[a,b],sampled_trace_rows=len(rs),positive_authority_rows=sum(float(r['authority'])>0 for r in rs),
  output_x_range=[min(float(r['out_x']) for r in rs),max(float(r['out_x']) for r in rs)],
  output_y_range=[min(float(r['out_y']) for r in rs),max(float(r['out_y']) for r in rs)]))
evidence['synthetic_open_loop_effects']=effects
(out/'diagnostic-evidence.json').write_text(json.dumps(evidence,indent=2))
print('portable trajectories',len(index),'frames',sum(r['frames'] for r in index))
print('association costs',evidence['friend_overlap'])
print(json.dumps(effects,indent=2))
