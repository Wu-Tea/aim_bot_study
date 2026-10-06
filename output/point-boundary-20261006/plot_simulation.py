"""Render native numerical experiment artifacts; no controller implementation."""
from pathlib import Path
import csv
import json
import shutil
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
SOURCE = ROOT / 'native/build/native-test-artifacts/base/BaseBodyLock'
for name in ('point-boundary.csv', 'point-loop-summary.csv', 'point-loop-trace.csv'):
    shutil.copyfile(SOURCE / name, OUT / name)
rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(open(OUT / 'point-loop-trace.csv'))]
summary = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(open(OUT / 'point-loop-summary.csv'))]
plt.rcParams.update({'font.family': 'Microsoft YaHei', 'axes.unicode_minus': False, 'font.size': 11})
fig, axes = plt.subplots(2, 2, figsize=(13, 7), sharex='col')
for col, (profile, motion, start, end, title) in enumerate([
    (0, 0, 500, 850, '静止目标、无噪声：20% 最小力度 / 2 px 到点半径'),
    (1, 2, 500, 2000, '目标匀速 90 px/s：30% 最小力度 / 12 px 到点半径'),
]):
    for variant, color, label in [(0, '#b34724', '现有最小力度'), (1, '#267e99', '对照：仅将最小力度设为 0')]:
        selected = [r for r in rows if r['profile'] == profile and r['motion'] == motion and r['variant'] == variant and start <= r['t_ms'] <= end]
        t = [r['t_ms'] for r in selected]
        axes[0, col].plot(t, [r['true_error_px'] for r in selected], color=color, label=label, linewidth=1.5)
        axes[1, col].plot(t, [r['output'] * 100 for r in selected], color=color, linewidth=1.3)
    radius = 2 if profile == 0 else 12
    axes[0, col].axhspan(-radius, radius, color='#e3e8ed', alpha=.7, label='到点范围')
    axes[0, col].set_title(title, fontsize=12, pad=12)
    axes[0, col].set_ylabel('真实目标点偏差 (px)')
    axes[1, col].set_ylabel('整形、死区处理后的 AI 输出 (%)')
    axes[1, col].set_xlabel('模拟时间 (ms)')
    for ax in axes[:, col]:
        ax.axhline(0, color='#777777', linewidth=.6)
        ax.grid(alpha=.18)
        ax.spines[['top', 'right']].set_visible(False)
    axes[0, col].legend(fontsize=9, loc='lower left')
fig.suptitle('到点边界数值模拟 · 使用现有原生求解器、速度估计器和输出整形器', fontsize=16)
fig.text(.5, .02, '视觉 240 FPS · 控制 1000 Hz · 视觉延迟 4 ms · 额外执行延迟 8 ms + 一步积分 · 线性镜头增益 1600 px/s/满杆\n单目标、单轴、无人工输入与后坐力；右图观测噪声 ±0.6 px。对照用于定位原因，不是建议配置。', ha='center', fontsize=9, color='#555555')
fig.tight_layout(rect=(0, .08, 1, .94))
fig.savefig(OUT / 'point-boundary-simulation.png', dpi=170)
fig.savefig(OUT / 'point-boundary-simulation.svg')
info = {
    'cases': len(summary),
    'control_steps': int(sum(r['duration_ms'] for r in summary)),
    'metrics_window': 'exclude first 500 ms of each run',
    'seeds': [1062026, 917331],
    'profile_0': 'floor 0.20, arrival 2 px, response 40 ms',
    'profile_1': 'floor 0.30, arrival 12 px, response 60 ms',
    'motion': {'0': 'static clean', '1': 'static noisy', '2': '90 px/s noisy', '3': '+/-90 px/s reverses every 750 ms, noisy'},
    'variant': {'0': 'configured floor', '1': 'floor zero ablation'},
    'assumptions': ['240 FPS vision, 1000 Hz control', '4 ms capture latency', '0/8/20 ms extra actuator delay plus 1 ms integration step', 'known linear plant gain 1600/1100 px/s per full stick', 'cap 80%, range 150 px, AI deadzone 3%', 'camera displacement compensation exactly known in observer', 'one fixed identity and horizontal axis; continuous visible BodyLock authority', 'no selector, cue transitions, recoil, manual arbitration, learning or nonlinear game response', 'not an exact Apex profile replay; no future predictor or new braking logic implemented'],
    'representative_rows': [r for r in summary if r['seed'] == 1062026 and r['duration_ms'] == 2000 and r['delay_ms'] == 8],
}
(OUT / 'experiment.json').write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'cases': info['cases'], 'control_steps': info['control_steps'], 'image': str(OUT / 'point-boundary-simulation.png')}, ensure_ascii=False))
