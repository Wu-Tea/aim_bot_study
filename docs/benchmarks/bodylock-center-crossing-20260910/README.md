# BodyLock 过零连续性修复 — 2026-09-10

状态：**局部事故回归 RED → GREEN，原生回归通过；实战验收待完成。**

## 症状、原因与修复边界

9 月 10 日审计中，同一目标 1081 / generation 340 / ADS epoch 148 的 X 误差由
-1.66666 变为 +0.666687 px，运动项仅由 0.185536 变为 0.201948，权限保持 1，
请求却由 -0.000758318 跳为 +0.216069。源记录为会话
`20260910T081959Z_30340_1` 的 telemetry-3 第 38605 / 38613 行，使用同会话
`output_sent_ns` 精确连接交付记录；没有使用有历史重发错位的 tick ID。
当时 Y 有人工操作且正在开火，不称作双轴纯 AI 对照。

最早违反连续性的位置在 `response_model_aim_solver.cpp`：反向运动项随位置逼近
零而被压到零，同向或 exact-zero 分支却释放完整运动项。下游 shaper 只能限制
这种请求跳变的传播，无法修复求解目标自身的不连续。

修复由现有求解器负责。位置项仍为 `p = error / (horizon * response)`：

- BodyLock 只有在现有 capture-aligned observer 给出有效且有限的总运动估计时，
  才设置 `motion_is_sustaining_target_motion`。同向持续运动完整保留；反向持续
  运动乘以 `1 - smoothstep(abs(p) / 0.12)`，在零点两侧连续衔接。
- 未确认的屏幕变化率不能拥有中心轴。同向变化率也乘以
  `smoothstep(abs(p) / 0.12)`；反向变化率保留原有位置约束。因此微小误差变号
  不再释放完整的未确认运动项。
- `0.12` 复用原有归一化位置邻域。这里的距离是屏幕误差经响应模型换算后的值，
  没有估计游戏世界中的目标距离。例：X horizon 80 ms、response 500 时，邻域边界
  为 4.8 px；响应估计变化时像素宽度也变化。
- ADS lookahead 和鼠标 point-tolerance 仍优先执行各自的位置规则。力限幅、
  authority、人工仲裁、生命周期、cue 和后坐力输出不在本次修改范围。

用户在解释取舍后确认了该方向：**可信持续运动可在中心邻域暂时与瞬时位置误差
反向；邻域外仍由位置纠正方向负责。** 全局“请求永不与瞬时误差反向”的规则
不能与“零误差时保留连续非零跟随”同时成立。

## 冻结回归与结果

测试调用生产 `BodylockFollowController` 和求解器，覆盖 X/Y、两种运动方向、
Linear / COD Dynamic、authority 0.65 / 1，共 16 组。每组在
`-0.001, 0, +0.001 px` 取样，固定总运动为 0.2 归一化响应命令。
另外验证中心附近持续需求、远处位置纠正、真实运动换向、生命周期 None、
零运动，以及 ±0.25 px 叠加未确认变化率的噪声对照。

| 冻结指标 | 旧实现 | 修复后 | 门槛 |
|---|---:|---:|---:|
| 过零最大请求跳变 | 0.299243301 | 0.0000275224 | ≤ 0.001 |
| ±1 px 内最小持续请求 / 中心请求 | -0.0412560 | 0.7290184 | ≥ 0.5 |
| 微小误差 + 未确认变化率的最大请求 | 0.254590183 | 0.027709087 | ≤ 0.03 |
| 仅微小位置误差的最大请求 | 0.023122268 | 0.023122268 | ≤ 0.03 |

这是请求值，不是测得的游戏相机速度、命中率或画面抖动幅度。负持续比例表示旧
实现曾把固定运动需求压到反向位置修正；没有用“关闭全部运动”使连续性测试变绿。
全部触发和负对照在 RED/GREEN 中有效。

生产改动前修正过一次 fixture：初稿 ±0.0001 px 落在旧代码 `1e-5` position-stick
的 exact-zero 特例内，未跨出缺陷边界，故改为 ±0.001 px 并增加触发断言；
初稿噪声门槛 0.02 也低于旧实现正常的 Dynamic 纯位置对照 0.0231223，故改为
0.03，避免把无关的位置增益调参混进修复。原始初稿结果保留在本机
`runs/bodylock-center-crossing-20260910/red/`。这两次修正均早于生产修改和候选输出。
此后 incident 测试函数与门槛保持不变；只更新了相邻旧单测对未确认正交运动的期望。

## 证据与复现

- [regression-manifest.json](regression-manifest.json)：事实、推断、未知项、协变量、
  RED/GREEN 可执行文件身份、命令和门槛。
- [red.json](red.json)、[green.json](green.json)：48 个位置探针和测量指标。
- [verification.json](verification.json)：Base 282/282、Functional 89/89，共 371 项通过。
  包括原有速度突变、开火/cue 延续、目标替换、人工接管、输出安全、AutoFire、
  后坐力等回归；另保护鼠标 point policy 与 ADS lookahead 的优先级。
- [artifacts.json](artifacts.json)、[contract-check.json](contract-check.json)：源码与
  报告哈希；complete 阶段合同检查 PASS。

在仓库根目录、完成 Release 构建后运行：

```powershell
native/vision_native/build/Release/cod_native_base_tests.exe --suite BaseBodyLock --case center_crossing_incident --artifacts runs/bodylock-center-crossing-review
python .agents/skills/incident-to-regression/scripts/regression_contract.py check --manifest docs/benchmarks/bodylock-center-crossing-20260910/regression-manifest.json --stage complete --output runs/bodylock-center-crossing-review/contract-check.json
```

构建目标是 `cod_native_base_tests`、`cod_native_functional_tests`、`cod_native_runtime`。
本机 MSBuild 需要在子进程环境中合并大小写重复的 PATH/Path，再用
`/p:Configuration=Release /p:Platform=x64 /m:1 /nr:false` 构建；这是构建环境修正，
未修改项目配置。完整差异已检查，未修改独立的 Vision benchmark 工作。

原生运行程序已重新构建：
`native/vision_native/build/Release/cod_native_runtime.exe`，SHA-256：
`14a80a5bf21f2acfca69f7a75370a7901dfafbe9e058898c2b7e7721a9a0d1d5`。
通过原有启动方式重启后才会使用新二进制；本次未启动或重启实战程序。
本机 `visual_authority_enabled=false` 保持原值，无新增配置项。

## 尚未解决的扰动与验收限制

本次没有改变 motion observer 的学习路径。它仍从 raw source-error 差分和
pre-recoil 命令账本估计运动；开火作用或检测框变形仍可能被它接纳为“有效目标
运动”。因此 `motion_is_sustaining_target_motion` 的资格沿用现有 observer，
不代表已独立证明无后坐力污染。这项证据隔离缺口仍然开放。

在 raw 差分已经混合相机运动、目标运动和几何变化时，单靠距离或多加一层低通
无法唯一识别来源。后续需要固定目标、可区分目标运动与开火作用的独立对照，
再选择记录实际输出工作或更上游的运动证据隔离；没有直接禁止开火期间全部运动
估计，以免破坏持续侧移跟随。当前用例中的 untrusted-rate 噪声注入不是 recoil
重放，不证明所有细微扰动已消除。

这份包建立的是无状态求解器的已知坏源码回归。RED 测试 exe 是新增 fixture 链接
未修改生产 owner 后得到的二进制，不冒充日志中的原始运行 exe；两个身份在 manifest
中分别记录。没有游戏 plant、画面时钟映射或匹配实战 A/B，不能宣告实战改善或
控制算法全面稳定。下一步验收应关注同场景侧移穿越中心时的连续性，以及开火时
剩余 Y 扰动与用户手感。
