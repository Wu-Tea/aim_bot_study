"""Candidate discovery, not an automated overshoot or player-intent verdict."""
import collections
import csv
import json
import pathlib

OUT=pathlib.Path('runs/hipfire-flick-20261023')
NUM='elapsed_s physical_x physical_y ai_x ai_y final_x final_y left_trigger right_trigger observed_error_x observed_error_y control_error_x control_error_y controller_target_track_id camera_response_scale bodylock_motion_stick_x bodylock_position_stick_x bodylock_effective_motion_stick_x stopping_reserve_x closing_rate_x region_x2 region_y2'.split()

def rows(path):
    with path.open() as f:
        for r in csv.DictReader(f):
            for k in NUM:r[k]=float(r[k])
            yield r

def main():
    results=[]
    for path in sorted((OUT/'ordered').glob('*.csv')):
        if not path.name.startswith('2026'):continue
        active={};events=[];history=collections.deque();stats=collections.Counter();previous=None;gaps=[]
        def finish(kind):
            e=active.pop(kind,None)
            if e and e['samples']>=2:
                e['duration_ms']=(e['end']['elapsed_s']-e['start']['elapsed_s'])*1000
                events.append(e)
        for r in rows(path):
            t=r['elapsed_s'];m=r['physical_x'];a=r['ai_x'];f=r['final_x'];error=r['observed_error_x'];target=r['controller_target_track_id']
            if previous:gaps.append((t-previous['elapsed_s'])*1000)
            while history and t-history[0]['elapsed_s']>.3:history.popleft()
            hip=r['left_trigger']<.1
            owned=r['has_target']=='True' and r['aim_authority']=='True' and r['output_delivered']=='True'
            delivered_ai=owned and abs(f-m)>.05 and a*f>0 and abs(a)>.1
            recent_flick=any(abs(p['physical_x'])>.65 for p in history)
            tests={
                'hip_ai_away_from_observed_point':hip and delivered_ai and error*f<0 and abs(error)>10,
                'hip_ai_after_flick_release':hip and delivered_ai and recent_flick and abs(m)<.15,
                'hip_ai_opposes_manual':hip and delivered_ai and abs(m)>.3 and m*f<0,
                'hip_flick_crossing':hip and owned and recent_flick and error*f<0 and abs(error)>10 and abs(f)>.3,
                'ads_ai_away_from_observed_point':not hip and delivered_ai and error*f<0 and abs(error)>10,
            }
            for kind,yes in tests.items():
                if kind in active and (not yes or t-active[kind]['end']['elapsed_s']>.035 or target!=active[kind]['end']['controller_target_track_id']):finish(kind)
                if yes:
                    stats[kind]+=1
                    if kind not in active:active[kind]=dict(kind=kind,start=r,end=r,peak=r,samples=0)
                    e=active[kind];e['end']=r;e['samples']+=1
                    if abs(f)>abs(e['peak']['final_x']):e['peak']=r
            history.append(r);previous=r
        for kind in list(active):finish(kind)
        gaps.sort()
        summary=dict(session=path.stem,stats=stats,sample_gap_ms={str(q):gaps[int(q*(len(gaps)-1))] for q in [.5,.9,.99,1]},events=sorted(events,key=lambda e:e['duration_ms']*abs(e['peak']['final_x']),reverse=True))
        results.append(summary)
        print(path.stem,'counts',dict(stats),'gaps',summary['sample_gap_ms'])
        for kind in ['hip_ai_away_from_observed_point','hip_ai_after_flick_release','hip_ai_opposes_manual','hip_flick_crossing']:
            print(kind)
            for e in [e for e in summary['events'] if e['kind']==kind][:5]:
                r=e['peak'];print(round(e['duration_ms'],1),r['local_time'],{k:r[k] for k in ['elapsed_s','physical_x','ai_x','final_x','observed_error_x','control_error_x','bodylock_motion_stick_x','bodylock_position_stick_x','aim_mode','enemy_cue_current','production_target_source']})
    (OUT/'candidates.json').write_text(json.dumps(results,indent=2))

if __name__=='__main__':main()
