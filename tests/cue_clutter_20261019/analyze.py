import csv,json,pathlib,collections
root=pathlib.Path(__file__).resolve().parents[2]/'runs/cue-clutter-20261019'
seeds=[2026101901,32452937,49979807,104395489]
comparison=[];summary={}
for noise,selected_seeds,folder in [(.03,seeds,root),(.01,seeds[2:],root/'noise01')]:
 for seed in selected_seeds:
  before=list(csv.DictReader((folder/'before'/str(seed)/'cases.csv').open()))
  after=list(csv.DictReader((folder/'after'/str(seed)/'cases.csv').open()))
  assert len(before)==len(after)==128
  for a,b in zip(before,after):
   assert (a['case'],a['duration'],a['variant'])==(b['case'],b['duration'],b['variant'])
   assert float(b['pair_max_output_difference'])==0, 'clutter affected output'
   if b['variant']=='0':
    assert all(a[k]==b[k] for k in b if k!='pair_max_output_difference'), 'reference behavior changed'
   else:
    comparison.append(dict(noise_fraction=noise,seed=seed,independent=seed in seeds[2:],case=int(b['case']),duration=int(b['duration']),before_late=float(a['late_mean']),after_late=float(b['late_mean']),delta=float(b['late_mean'])-float(a['late_mean']),before_cues=int(a['cues']),before_accepted=int(a['accepted_cues']),after_cues=int(b['cues']),after_accepted=int(b['accepted_cues']),before_zero=int(a['zero_authority']),after_zero=int(b['zero_authority']),before_max_delta=float(a['max_delta']),after_max_delta=float(b['max_delta']),before_body=int(a['body']),after_body=int(b['body']),after_exits=int(b['manual_exits'])))
for key,data in [('all',comparison),('independent',[r for r in comparison if r['independent']]),('independent_noise03',[r for r in comparison if r['independent'] and r['noise_fraction']==.03]),('independent_noise01',[r for r in comparison if r['independent'] and r['noise_fraction']==.01])]:
 summary[key]=dict(paired_cases=len(data),improved=sum(r['delta']<-.001 for r in data),worsened=sum(r['delta']>.001 for r in data),flat=sum(abs(r['delta'])<=.001 for r in data),before_cues=sum(r['before_cues'] for r in data),before_accepted=sum(r['before_accepted'] for r in data),after_cues=sum(r['after_cues'] for r in data),after_accepted=sum(r['after_accepted'] for r in data),before_zero=sum(r['before_zero'] for r in data),after_zero=sum(r['after_zero'] for r in data),worst_regressions=sorted(data,key=lambda r:r['delta'],reverse=True)[:6],best_improvements=sorted(data,key=lambda r:r['delta'])[:4])
for version in ['before','after']:
 rows=list(csv.DictReader((root/f'video-{version}.jsonl.bridge.csv').open()))
 summary['video_'+version]=dict(cue_frames=len(rows),accepted=sum(int(r['actual_cue']) for r in rows),clutter_frames=[r for r in rows if int(r['residual_count'])])
with (root/'comparison.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(comparison[0]));w.writeheader();w.writerows(comparison)
(root/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
