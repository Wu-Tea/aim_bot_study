"""Compact observed inputs from the preserved windows; no invented video labels."""
import csv
import json
from pathlib import Path
from review_windows import load
from scan import OUT

DEST=Path(__file__).parent/'fixtures'
def main():
    DEST.mkdir(exist_ok=True)
    index=[]
    for path in sorted((OUT/'windows').glob('*.jsonl')):
        meta,rows,observations,_=load(path)
        origin=rows[0]['sample_ns'];name=path.stem
        observations.sort(key=lambda r:r['observation']['captured_at_ns'])
        latest={};j=0
        with (DEST/(name+'.csv')).open('w',newline='') as f:
            writer=csv.writer(f)
            writer.writerow('t,mx,my,lt,fire,id,valid,ex,ey,width,height,cue,request_x,request_y,ai_x,ai_y,out_x,out_y,confidence,mode,continuation'.split(','))
            for r in rows:
                while j<len(observations) and observations[j]['observation']['captured_at_ns']<=r['sample_ns']:
                    o=observations[j]['observation'];latest[o['persistent_target_id']]=o;j+=1
                ident=r['controller_target_track_id'];o=latest.get(ident)
                region=r['aim_region'];w,h=(o['raw_body_box'][2:4] if o else [region[2]-region[0],region[3]-region[1]])
                writer.writerow([(r['sample_ns']-origin)/1e9,r['physical_x'],r['physical_y'],r['left_trigger'],int(r['final_fire_button']),ident,int(r['has_target']),r['observed_error_x'],r['observed_error_y'],max(10,w),max(10,h),int(r['enemy_cue_current']),r['requested_assist_x'],r['requested_assist_y'],r['ai_x'],r['ai_y'],r['final_x'],r['final_y'],r['production_target_confidence'],r['aim_mode'],int(r['production_target_source']=='cue_hold')])
        index.append(dict(name=name,source_session=path.name.split('-')[0],origin_steady_ns=origin,clock_utc_ns=meta['clock_utc_ns'],clock_steady_ns=meta['clock_steady_ns'],samples=len(rows)))
    (DEST/'index.json').write_text(json.dumps(index,indent=2))
    print('fixtures',len(index),'samples',sum(r['samples'] for r in index))
if __name__=='__main__':main()
