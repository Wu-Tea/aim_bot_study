"""Summarize observed test/replay outputs; does not establish live acceptance."""
import json,re,statistics,html
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'runs/incident-20261102/fix'
rounds=[json.loads((OUT/f'performance-{i}.json').read_text()) for i in range(1,4)]
performance={}
for version in ['before','after']:
 means=[]
 for rows in rounds:
  selected=[r for r in rows if r['version']==version]
  means.append(sum(r['mean_ms']*(r['frames']-10) for r in selected)/sum(r['frames']-10 for r in selected))
 performance[version]={'round_mean_ms':means,'median_round_mean_ms':statistics.median(means)}
cases=[]
for name in dict.fromkeys(r['case'] for r in rounds[0]):
 rows={v:[r for rs in rounds for r in rs if r['version']==v and r['case']==name] for v in ['before','after']}
 cases.append({'case':name,'frames':rows['after'][0]['frames'],**{v:{'target':rows[v][0]['target'],'friendly':rows[v][0]['friendly'],'mean_ms':statistics.median(r['mean_ms'] for r in rows[v]),'p95_ms':statistics.median(r['p95_ms'] for r in rows[v])} for v in rows}})
tests={}
for name in ['base','functional']:
 text=(OUT/f'{name}-full.txt').read_text(encoding='utf-8-sig')
 matches=re.findall(r'selected=(\d+) passed=(\d+) failed=(\d+) invalid=(\d+)',text)
 assert matches and matches[-1][2:]==('0','0'),text[-500:]
 tests[name]=dict(zip(['selected','passed','failed','invalid'],map(int,matches[-1])))
adjacent=json.loads((OUT/'adjacent/summary.json').read_text())
result=dict(baseline=(OUT/'baseline/revision.txt').read_text().strip(),tests=tests,performance=performance,cases=cases,adjacent=[dict(case=r['case'],frames=r['frames'],gained=len(r['gained']),lost=len(r['lost'])) for r in adjacent],live_validated=False,deployed=False)
(OUT/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
table=''.join(f"<tr><td>{r['case']}</td><td>{r['frames']}</td><td>{r['before']['target']} → {r['after']['target']}</td><td>{r['before']['mean_ms']:.4f} → {r['after']['mean_ms']:.4f}</td><td>{r['before']['p95_ms']:.4f} → {r['after']['p95_ms']:.4f}</td></tr>" for r in cases)
before=performance['before']['median_round_mean_ms'];after=performance['after']['median_round_mean_ms']
body=f'''<!doctype html><meta charset="utf-8"><title>11 月 2 日视觉修复结果</title>
<style>body{{font:16px/1.7 system-ui;max-width:1000px;margin:40px auto;padding:0 24px;color:#243143}}h1,h2{{line-height:1.3}}table{{border-collapse:collapse;width:100%}}td,th{{text-align:left;padding:9px;border-bottom:1px solid #ddd}}.note{{background:#f2f5f9;padding:16px}}a{{color:#165ab7}}</style>
<h1>视觉修复已通过离线验证，候选程序尚未部署</h1>
<p>基于当前 dev <code>{html.escape(result['baseline'])}</code>。没有切分支、恢复 1631041 实验控制器或覆盖原实战 EXE。日志中的历史构建号只用于调查；本次前后对照统一使用当前 dev 基线。</p>
<h2>改动与证据</h2><ul>
<li>先见人、后见 cue：近期可靠人物提供局部关联，当前 cue 才能接续输出；临时身体参考被重新出现的真实人框替换。无当前 cue、歧义、过期或重置均不能续出目标，cue 接续没有开火权限。</li>
<li>03:04 柱子案例：用完整友军标记替代单纯颜色占比否决，消除青色墙面/头盔碎片误判。友军开关、自定义颜色及小/暗友军正例保留。</li>
<li>09:06 远处目标：首次获取与原跟踪几何下限统一，22.5×39.5 px 记录框不再因更高的首次门槛被拒；原跟踪下限保留，避免头部碎片抢占。</li>
<li>25% 点在空气：当前可靠人框与唯一 cue 配对时更新身体参考，保留对标记残框、逐帧膨胀的保护；没有把 25% 改成其他位置。</li></ul>
<h2>验证</h2><p>基础测试 {tests['base']['passed']}/{tests['base']['selected']}、功能测试 {tests['functional']['passed']}/{tests['functional']['selected']} 通过。4 组专项用例在修复前均失败，修复后通过；含四个固定随机种子、短/长序列、遮挡、重新出现、友军、手动权限及生命周期相邻行为。</p>
<p>五段历史录像共 {sum(r['frames'] for r in adjacent):,} 帧，新增丢失帧 {sum(len(r['lost']) for r in adjacent)}。这只说明固定输入下的目标有无对照，不能代替身份和准确率真值。<a href="adjacent/friendly-review.jpg">友军过滤变化的画面抽查</a>。</p>
<h2>本次录像回放及成本</h2><table><tr><th>片段</th><th>帧数</th><th>有目标帧</th><th>平均 ms</th><th>P95 ms</th></tr>{table}</table>
<p>无构建/测试并行的三轮回放，交替输入顺序；去掉各窗口前 10 帧预热。全窗口加权平均耗时的三轮中位数：{before:.4f} → {after:.4f} ms（增加 {after-before:.4f} ms/帧）。成本主要来自局部友军形状检查和延迟 cue 搜索。没有增加全帧搜索或改变检测频率。</p>
<p class="note">这些时间仅测 CPU 选择器，不包括 GPU 推理、采集、回读或控制链，不能据此声称实战 FPS。120 fps 压缩录像与约 200 fps 原采集不一致；校准容差约 ±25 ms。E76 的首次获取在离线像素回放中早于原日志，所以不以目标帧数证明原始全部缺口消失；该故障另有相同记录坐标的数值复现。</p>
<h2>尚未解决</h2><p>垂直跟随 E70 的日志含腰射倍率、手动仲裁、压枪叠加，尚未分离真实视角响应、游戏反冲和目标运动。没有足够证据确定控制修改，不能把本次视觉修复称为垂直响应修复。未修改控制器、Profile、力上限或手动权限。</p>
<p>候选程序：<a href="runtime-build/Release/cod_native_runtime.exe">单独构建的 EXE</a>。原 <code>native/build/Release/cod_native_runtime.exe</code> 保持原样，修改尚未进入实战程序。</p>
<p><a href="result.json">结构化结果</a> · <a href="base-full.txt">基础测试</a> · <a href="functional-full.txt">功能测试</a> · <a href="../loss-search/review.html">用户标记案例</a> · <a href="../analysis/findings.html">此前根因调查</a></p>'''
(OUT/'report.html').write_text(body,encoding='utf-8');print(json.dumps(result,ensure_ascii=False,indent=2))
