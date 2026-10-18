import csv,json,statistics as st,sys
from pathlib import Path
r=Path(sys.argv[1]);pairs=[]
for p in sorted((r/'final-after').glob('seed*.csv')):
 a=list(csv.DictReader(p.open()));b=list(csv.DictReader((r/'final-before'/p.name).open()));assert len(a)==len(b)==64
 pairs+=list(zip(b,a))
assert len(pairs)==256
result={'development_seeds':[2026101819,32452867],'independent_validation_seeds':[104395303,67867979],'new_cases':len(pairs),'new_ticks':sum(int(x['duration_ms']) for x,y in pairs),'old_mixed_cases':128,'old_mixed_ticks':1408000,'recorded_source_pairs':50,'recorded_solver_cases':92,'groups':[],'live_game_acceptance':False}
for kind in range(4):
 ps=[(x,y) for x,y in pairs if int(x['kind'])==kind]
 group={'kind':kind,'cases':len(ps),'improved_mean_error':sum(float(y['mean_error'])<float(x['mean_error']) for x,y in ps),'metrics':{}}
 for key in ['mean_error','mean_late_error','within_4_fraction','center_crossings','first_20_ms','first_2_ms']:
  group['metrics'][key]={'before':st.mean(float(x[key]) for x,y in ps if float(x[key])>=0),'after':st.mean(float(y[key]) for x,y in ps if float(y[key])>=0),'before_missing':sum(float(x[key])<0 for x,y in ps),'after_missing':sum(float(y[key])<0 for x,y in ps)}
 result['groups'].append(group)
 print(kind,len(ps),group['improved_mean_error'],{k:{k2:round(v2,3) for k2,v2 in v.items()} for k,v in group['metrics'].items()})
result['new_missing_20']=[{'before':x,'after':y} for x,y in pairs if float(y['first_20_ms'])<0<=float(x['first_20_ms'])]
(r/'comparison.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
