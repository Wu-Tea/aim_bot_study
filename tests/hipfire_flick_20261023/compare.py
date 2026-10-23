"""Summarize measured tradeoffs; no score or live-game acceptance verdict."""
import csv
import json
import statistics
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=Path('runs/hipfire-flick-20261023')
def read(p):
    with p.open() as f:return list(csv.DictReader(f))

def main():
    result={}
    before=read(ROOT/'perf-before-2/summary.csv');after=read(ROOT/'perf-after-2/summary.csv')
    changes=[]
    for b,a in zip(before,after):
        assert (b['case'],b['plant'])==(a['case'],a['plant'])
        assert b['acquired_tick']==a['acquired_tick'] and b['owned']==a['owned']
        assert a['source_violations']==a['manual_violations']=='0'
        changes.append(dict(case=a['case'],plant=int(a['plant']),before=float(b['mean_error']),after=float(a['mean_error']),late_before=float(b['late_error']),late_after=float(a['late_error'])))
    result['log_cases']=changes
    result['timings']={}
    for version in ['before','after']:
        values=[]
        for repeat in range(3):
            rows=read(ROOT/f'perf-{version}-{repeat}/summary.csv')
            values.append(dict(weighted_mean_us=sum(float(r['controller_us'])*int(r['ticks']) for r in rows)/sum(int(r['ticks']) for r in rows),median_case_p95_us=statistics.median(float(r['controller_p95_us']) for r in rows)))
        result['timings'][version]=values
    result['randomized']=[]
    for seed in [231023,981027]:
        b=read(ROOT/f'random-before-{seed}/cases.csv');a=read(ROOT/f'random-after-{seed}/cases.csv')
        pairs=[dict(case=int(y['case']),duration=int(y['duration']),manual=int(y['manual']),before=float(x['mean_error']),after=float(y['mean_error']),peak_before=float(x['peak_delta']),peak_after=float(y['peak_delta'])) for x,y in zip(b,a)]
        result['randomized'].append(dict(seed=seed,cases=pairs,mean_before=statistics.mean(x['before'] for x in pairs),mean_after=statistics.mean(x['after'] for x in pairs)))
    plt.rcParams['font.sans-serif']=['Microsoft YaHei','DejaVu Sans'];plt.rcParams['axes.unicode_minus']=False
    fig,axes=plt.subplots(2,1,figsize=(10,7))
    result['component_handoff']={}
    for version,label,color in [('before','优化前','#dc2626'),('after','优化后','#0284c7')]:
        rows=[r for r in read(ROOT/f'perf-{version}-2/ownership.csv') if '080531' in r['case'] and '867.7' in r['case'] and .78<=float(r['t'])<=.81]
        peak=max(abs(float(b['ox'])-float(a['ox'])) for a,b in zip(rows,rows[1:]))
        settled=next(float(r['t']) for r in rows if float(r['t'])>.785 and abs(float(r['ox'])-float(r['aix']))<1e-5)
        result['component_handoff'][version]=dict(peak_1ms_step=peak,settled_s=settled)
        axes[0].plot([(float(r['t'])-.78)*1000 for r in rows],[float(r['ox']) for r in rows],label=label,color=color)
        if version=='before':axes[0].plot([(float(r['t'])-.78)*1000 for r in rows],[float(r['mx']) for r in rows],label='同一手动回中输入',color='#78716c',ls='--')
        scope=read(ROOT/f'perf-{version}-2/scope.csv')
        axes[1].plot([int(r['tick']) for r in scope],[float(r['output']) for r in scope],label=label,color=color)
    axes[0].set_title('日志回中轨迹的权限接管复算（AI 提议固定为记录值）')
    axes[1].set_title('腰射 0.7 → ADS 1.0 倍率隔离检查（恒定请求 −0.78）')
    axes[1].axvline(40,color='#78716c',ls='--',label='重新 ADS')
    for ax in axes:ax.set_xlabel('毫秒');ax.set_ylabel('输出 X');ax.grid(alpha=.2);ax.legend()
    fig.tight_layout();fig.savefig(ROOT/'optimization-comparison.png',dpi=150);plt.close(fig)
    (ROOT/'comparison.json').write_text(json.dumps(result,indent=2))
    print('handoff',result['component_handoff'])
    print('32 log cases preserve acquisition tick, owned coverage, source point and manual exit invariants')
    print('log stress regressions',[(r['case'],r['plant'],round(r['after']-r['before'],4)) for r in changes if r['after']>r['before']+.01])
if __name__=='__main__':main()
