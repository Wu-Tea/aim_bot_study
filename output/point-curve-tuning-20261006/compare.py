"""Compare unchanged deterministic plant scenarios before/after the fix."""
import csv
import json
import shutil
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[1]
OLD=ROOT/'output/point-boundary-20261006'
NEW=ROOT/'native/build/native-test-artifacts/base/BaseBodyLock'
def read(path):
    return [{k:float(v) for k,v in row.items()} for row in csv.DictReader(open(path))]
for name in ('point-loop-summary.csv','point-loop-trace.csv','point-boundary.csv'):
    shutil.copyfile(NEW/name,OUT/name)
before,after=read(OLD/'point-loop-summary.csv'),read(OUT/'point-loop-summary.csv')
keys=('seed','profile','motion','delay_ms','duration_ms','variant')
old={tuple(r[k] for k in keys):r for r in before}
comparison=[]
for r in after:
    if r['variant']!=0: continue
    b=old[tuple(r[k] for k in keys)]
    row={k:r[k] for k in keys}
    for metric in ('mean_abs_error_px','rms_error_px','peak_error_px','output_zero_edges','output_reversals','output_total_variation'):
        row['before_'+metric]=b[metric];row['after_'+metric]=r[metric]
    comparison.append(row)
with open(OUT/'comparison.csv','w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(comparison[0]));writer.writeheader();writer.writerows(comparison)
result={'paired_configured_floor_cases':len(comparison),'improved_mean_error_cases':sum(r['after_mean_abs_error_px']<r['before_mean_abs_error_px'] for r in comparison),
    'not_improved_cases':[r for r in comparison if r['after_mean_abs_error_px']>=r['before_mean_abs_error_px']],
    'scope':'same 192 plant cases and 1,344,000 steps as baseline; 96 paired configured-floor cases, 96 zero-floor ablations; no full-runtime replay or live acceptance',
    'changes':['continuous position braking budget, 25 ms nominal stopping horizon','configured arrival radius is now a smooth approach scale; ADS completion radius unchanged','five capture-interval displacement history, maximum 25 ms; existing rise/decay and identity rules retained'],
    'not_implemented':['new optical flow','10-frame / 2-frame trajectory extrapolator','extra output return delay'],
    'residual':'3% per-axis AI deadzone can still interrupt small commands; sensor noise, unknown plant delay and cue/source transitions remain fidelity gaps'}
(OUT/'comparison.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
plt.rcParams.update({'font.family':'Microsoft YaHei','axes.unicode_minus':False,'font.size':10})
fig,axs=plt.subplots(2,3,figsize=(15,7),sharex='col')
traces=[read(OLD/'point-loop-trace.csv'),read(OUT/'point-loop-trace.csv')]
for col,(profile,motion,title,end) in enumerate([(0,0,'静止无噪声 · 20% / 2 px',850),(0,1,'静止有噪声 · 20% / 2 px',2000),(1,2,'移动 90 px/s · 30% / 12 px',2000)]):
    for rows,color,label in zip(traces,['#b34724','#267e99'],['修改前','修改后']):
        selected=[r for r in rows if r['variant']==0 and r['profile']==profile and r['motion']==motion and 500<=r['t_ms']<=end]
        t=[r['t_ms'] for r in selected]
        axs[0,col].plot(t,[r['true_error_px'] for r in selected],color=color,label=label,lw=1.2)
        axs[1,col].plot(t,[100*r['output'] for r in selected],color=color,lw=1.1)
    axs[0,col].set_title(title,pad=12);axs[0,col].legend()
    axs[0,col].set_ylabel('真实目标点偏差 (px)');axs[1,col].set_ylabel('AI 输出 (%)');axs[1,col].set_xlabel('模拟时间 (ms)')
    for ax in axs[:,col]:
        ax.axhline(0,color='#888888',lw=.6);ax.grid(alpha=.15);ax.spines[['top','right']].set_visible(False)
fig.suptitle('连续收尾曲线与短历史速度估计 · 相同场景修改前后对比',fontsize=17)
fig.text(.5,.02,'视觉 240 FPS / 控制 1000 Hz · 视觉延迟 4 ms · 执行额外延迟 8 ms + 一步积分 · 镜头增益 1600 px/s/满杆\n有噪声场景为 ±0.6 px。完整结果包含另一独立种子、±1.2 px 噪声、不同延迟及 12 秒长场景；图中不是实机记录。',ha='center',fontsize=9,color='#555555')
fig.tight_layout(rect=(0,.08,1,.94));fig.savefig(OUT/'before-after.png',dpi=160);fig.savefig(OUT/'before-after.svg')
print(json.dumps({'paired_cases':len(comparison),'improved_mean_error_cases':result['improved_mean_error_cases'],'not_improved_cases':len(result['not_improved_cases'])}))
