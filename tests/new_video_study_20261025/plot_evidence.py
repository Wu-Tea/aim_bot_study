import csv,json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path(__file__).resolve().parents[2];out=root/'runs/new-video-study-20261025'
plt.rcParams.update({'font.family':'Microsoft YaHei','axes.unicode_minus':False})
rs=[json.loads(s) for s in (out/'v07/vision.jsonl').read_text().splitlines()]
part=[r for r in rs if 180.9<=r['t']<=181.85]
obs=[r for r in part if r['cue_reason']=='person_cue_reference_recorded']
fig,axes=plt.subplots(2,1,figsize=(11,6.6),constrained_layout=True)
ax=axes[0]
ax.plot([r['t'] for r in part],[r['target'][1]-r['motion_cue'][2] if r['has_target'] and r['motion_cue'][0]==67 else float('nan') for r in part],color='#b32b83',label='输出瞄点到 cue 的纵向距离（缺失处断开）')
ax.scatter([r['t'] for r in obs],[r['body'][1]-r['motion_cue'][2] for r in obs],s=16,color='#197b94',label='获准重标定的框顶到 cue 距离')
ax.axhline(0,color='#999999',lw=.8);ax.set(xlabel='v07 录像时间（秒）',ylabel='像素；正数向下',title='实拍：框顶逐渐扩入背景后，错误偏移被保存')
ax.legend(loc='lower left');ax.grid(alpha=.2)
probe=list(csv.DictReader((out/'geometry-probe.csv').open(encoding='utf-8-sig')))
ax=axes[1];ax.plot([float(r['aim_y'])-230 for r in probe],label='当前原生 selector 输出',color='#b32b83')
ax.axhline(36,color='#197b94',ls='--',label='固定真实人物的初始瞄点距离（36 px）');ax.axvline(37.5,color='#777',ls=':')
ax.text(39,24,'恢复原始人体框\n仍保留错误偏移',fontsize=10)
ax.set(xlabel='独立探针帧序号（每帧 10 ms）',ylabel='瞄点到 cue 距离 / px',title='根因复现：人物与 cue 不动，仅检测框顶每帧上扩 1 px')
ax.legend(loc='upper right');ax.grid(alpha=.2)
fig.savefig(out/'geometry-drift-evidence.png',dpi=150);plt.close(fig)
