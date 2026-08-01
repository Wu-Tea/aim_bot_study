# ADS / BodyLock 强跳变稳定性修复计划

> 面向下一 Codex session。按任务顺序执行；第一轮只处理已有直接证据的
> fusion 连续性和 ADS→BodyLock 状态交接。不要同时重构 selector、tracker、
> response model、Remaining 和 recoil。

## 完成状态（2026-08-01）

本计划已经执行完毕，下面的逐项清单保留为实施前的历史合同，不再代表当前待办：

- [x] Task 0：现场 runtime/config/engine/source 身份已锁定并保留回退。
- [x] Task 1：VectorIntentFuser 硬退出/冷启动跳变已修复并通过 focused 测试。
- [x] Task 2：ADS→BodyLock 和 target-change 状态交接已修复并通过 focused 测试。
- [x] Task 3：Release `34/34`、focused tests、`git diff --check` 和 matched A/B 通过。
- [x] Task 4：held-LT replacement target admission 已完成；Fresh Vision 与合法
  same-track 横移仍保持 authoritative。
- [x] Candidate/runtime：用户明确授权覆盖，旧 runtime 已备份，新 runtime 已完成实机验收。

最终验收见
[CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md](../../project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md)，
Task 4 数字证据见
`artifacts/benchmarks/control-chain-jump-fix-20260801/task4-ownership-admission/VERIFICATION.md`。

**目标：** 消除当前实验运行时中 ADS 突然拉飞、BodyLock 突然跳远和 AI
输出全开/全关的问题，同时保留正常 ADS 到达速度、BodyLock 跟随力度和手动逃逸。

**控制链：** `Vision/selector -> TargetCoordinator -> ADS or BodyLock request ->
AimDynamicsShaper -> VectorIntentFuser -> recoil -> virtual gamepad`。

**首轮架构边界：** TargetCoordinator 继续拥有目标、生命周期和可靠度；
AimDynamicsShaper 只处理 AI 请求动态；VectorIntentFuser 继续是唯一最终
`manual + AI` 连续性所有者。不得新增第二个输出 brake、可靠度 gate 或 tracker。

**技术栈：** C++17、MSVC/CMake Release、当前 native telemetry、现有 sustained
AimLab/micro fixtures。

---

## 0. 先锁定现场与工作区边界

当前工作区有大量用户所有的未提交控制实验，不能 reset、checkout 覆盖或批量回滚。
最新现场也不是 7 月 31 日已验收运行时：

- 最新会话：
  `runs/native_perf/sessions/20260731T193527Z_29400_1/`
- 会话记录 HEAD：`5d9f6d37703d5b0d2af009ae5389ee5d95671710`
- 现场 executable SHA-256：
  `3D9ED74CBCD7007F9EEF3CACC6B17EB5763F7056047BE35AC37495BF1CEB7402`
- 7 月 31 日已验收 executable SHA-256：
  `80831B5197B8AC58C20844A349BD673FCC21B2768A055AB4446911257BA70F50`
- manifest 没有记录 dirty source fingerprint；不能仅凭 `build_commit` 证明源码身份。

### Task 0：建立不覆盖现场的基线

**只读检查：**

- [ ] 先读 `.agent-context/handoff.md`、`.agent-context/session-log.md` 和
  `.agent-context/decisions/DEC-2026-07-31-001-separate-target-motion-and-firing-disturbance.md`。
- [ ] 读本计划以及下列当前实现：
  - `native/controller_native/vector_intent_fuser.cpp`
  - `native/controller_native/aim_dynamics_shaper.cpp`
  - `native/controller_native/native_gamepad_controller.cpp`
  - `native/controller_native/target_coordinator.cpp`
- [ ] 记录 `git status --short`、相关文件 diff、当前 runtime/config/engine hash。
- [ ] 确认所有现有 dirty 文件和 benchmark artifact 都按用户工作保留；不得用
  `git reset --hard`、`git checkout --` 或覆盖式复制恢复基线。
- [ ] 测试阶段只构建 focused test targets。需要完整 runtime 时，使用独立
  candidate 输出或先保留当前 executable；未获得用户明确许可不得替换、重启或部署
  用户正在使用的 runtime。

**成功条件：** 能明确区分当前现场 executable、当前 dirty source、修复 candidate
三种身份，后续 artifact 都记录 source diff/config/engine/runtime hash。

---

## 1. 第一优先级：修复 VectorIntentFuser 的硬退出和冷启动

### 已证实缺陷

当前 `manual_only()` 会：

1. 立即把 `fused_stick` 切为纯手动；
2. 把 `previous_output_` 改为手动；
3. 把 `output_initialized_` 清为 `false`；
4. 在可靠度恢复时直接以完整融合结果重新初始化。

`input.plan.reliability < 0.65`、`Reacquiring`、`TargetChanged` 和 `NoTarget`
等 early-return 都绕过正常 final-vector slew。TargetCoordinator 又会在 Vision 帧间
连续衰减可靠度，因此当前实现把连续可靠度变成 AI 全开/全关。

现场最后人物窗口的 `post_ai` 证据：

| 视频 | AI 参与/退出切换 | 最大单步 post-fusion 向量变化 |
|---|---:|---:|
| `03.48.10.167` | 20 | 0.635 |
| `03.49.15.168` | 1 | 1.147 |
| `03.52.41.169` | 0 | 0.194 |
| `03.53.15.170` | 6 | 0.588 |

### Task 1A：先写失败测试

**文件：**

- Modify: `native/controller_native/vector_intent_fuser_tests.cpp`
- 如需暴露最小状态：Modify: `native/controller_native/vector_intent_fuser.h`

- [ ] 增加同一 target、同一 mode、连续 manual/AI 下的可靠度序列测试，例如
  `0.70 -> 0.64 -> 0.68 -> 0.63 -> 0.72`。
- [ ] 证明在 `0.65` 两侧摆动时不会出现完整 AI 一拍消失、一拍恢复。
- [ ] 低可靠度释放阶段必须保持有限输出变化；AI 恢复阶段每 tick 不得超过现有
  reassert 上限 `0.08`。
- [ ] 增加 `Reacquiring -> Observed` 测试，证明恢复时不能 cold-start 到完整 AI。
- [ ] 增加 `NoTarget -> new target while LT held` 测试：无目标时仍需精确传递物理
  manual；新目标 AI 必须从 manual 基线渐入，不能首 tick 满量程。
- [ ] 增加 `TargetChanged` 测试：新目标首 tick 不得继承旧目标 AI；后续新目标
  AI 仍须受 re-entry slew 限制。
- [ ] 保留 deliberate manual escape 的精确物理输入语义，不允许为了平滑而粘住用户。
- [ ] 先运行测试并确认至少可靠度摆动、reacquire 和 no-target re-entry 用例为 RED。

### Task 1B：最小修复，不恢复另一套复杂控制器

**文件：**

- Modify: `native/controller_native/vector_intent_fuser.cpp`
- Modify: `native/controller_native/vector_intent_fuser.h`

- [ ] 移除或连续化 `reliability < 0.65` 的二次硬 gate。TargetCoordinator/ADS/
  BodyLock 已经通过 `plan.reliability` 缩放 authority；fuser 不再重复拥有可靠度策略。
- [ ] 将“选择 manual 目标值”和“清空输出连续状态”拆开。短暂低可靠度或
  reacquiring 不得把 `output_initialized_` 清零。
- [ ] `NoTarget`/Manual 模式仍可精确输出物理 manual，但要保留可用于下一目标
  re-entry 的 manual 基线，不能让下一 tick 冷启动完整 AI。
- [ ] `TargetChanged` 不继承旧目标 AI；允许立即或快速释放旧 AI，但新目标 AI
  必须通过同一个 final-vector slew 重新进入。
- [ ] 输出连续性必须继续只有一个 owner；不要在 NativeGamepadController 后面
  再加 limiter/brake。
- [ ] 不要整体恢复旧的候选评分栈，除非先用 focused A/B 证明最小修复无法满足合同。
- [ ] focused fuser tests 全绿，并确认原有 manual escape、rotational invariance、
  finite fallback 测试仍绿。

**Task 1 完成门：** 可靠度跨阈值、短 reacquire、target change、no-target re-entry
都不再产生 full AI off/on impulse；精确 manual passthrough 不回归。

---

## 2. 第二优先级：阻止 ADS 饱和状态进入首个 BodyLock tick

### 已证实缺陷

视频 `03.52.41.169` 的最后人物窗口中：

- ADS 最后阶段 `requested.x ~= 1.354`，`shaped.x ~= 1.342`；
- 切换 BodyLock 后 `requested.x = 0.500`，但首 tick `shaped.x = 1.182`；
- 约 20 ms 后旧 ADS 力才衰减到 BodyLock 请求附近。

TargetCoordinator 已在 ADS→BodyLock 时清掉 Remaining，但共享
AimDynamicsShaper 不知道 mode/target 边界，仍携带旧 ADS AI 状态。

### Task 2A：锁定 handoff 合同

**文件：**

- Modify: `native/controller_native/aim_dynamics_shaper_tests.cpp`
- Modify: `native/controller_native/aim_dynamics_shaper.h`
- 必要时 Modify: `native/controller_native/target_pipeline_integration_tests.cpp`

- [ ] 构造同一 target 的 ADS 饱和输出 `~1.34`，下一 tick 切换为 BodyLock 请求
  `~0.50`，先证明当前首个 BodyLock 输出仍明显大于新请求。
- [ ] 合同要求：首个 BodyLock tick 不得继续输出 ADS 饱和力；单分量不得超过
  新 BodyLock 请求超过 `0.08`，或二维 `|shaped| / |requested|` 不得超过 `1.15`。
- [ ] 构造 ADS→BodyLock 同方向、反方向、带 Y 分量三种用例，避免只修 X 轴。
- [ ] 构造 target id 变化用例，证明新目标不继承旧目标 shaper 状态。
- [ ] 保留同一 mode 内既有 rise/decay 和两阶段 reversal 语义。

### Task 2B：实现 mode/target-aware handoff

**文件：**

- Modify: `native/controller_native/aim_dynamics_shaper.cpp`
- Modify: `native/controller_native/aim_dynamics_shaper.h`
- 如调用合同需要：Modify: `native/controller_native/native_gamepad_controller.cpp`

- [ ] 在 shaper 内记录最小的 previous target id / control mode，或由调用方显式提交
  transition；不要引入新 lifecycle owner。
- [ ] ADS→BodyLock 时，将旧 ADS AI 状态约束到新 BodyLock 请求包络；不能让旧
  ADS 饱和值作为首个 BodyLock 输出。
- [ ] target id 变化时清除旧目标 AI 状态；不要把旧方向平滑成新目标方向。
- [ ] Manual/None 继续向零释放；不得影响物理 manual passthrough，因为 manual
  融合仍由 fuser 所有。
- [ ] 最终可见输出的平滑仍由修复后的 VectorIntentFuser 保证，不新增另一层
  post-output memory。
- [ ] focused shaper、fuser、controller integration tests 全绿。

**Task 2 完成门：** 日志式 fixture 中不再出现 `BodyLock requested 0.50 / shaped
1.18`；正常 ADS 到达速度和同 mode BodyLock 跟随不回归。

---

## 3. 集成验证：先证明前两项已解释主要跳变

### Task 3A：Focused build/test

推荐 PowerShell 变量：

```powershell
$cmakeExe = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctestExe = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$buildDir = 'D:\work\AI\yolo-study-001\native\vision_native\build'
```

- [ ] 构建 focused targets：

```powershell
& $cmakeExe --build $buildDir --config Release --target `
  cod_native_vector_intent_fuser_tests `
  cod_native_aim_dynamics_shaper_tests `
  cod_native_controller_tests `
  cod_native_target_coordinator_tests `
  cod_native_bodylock_follow_controller_tests
```

- [ ] 直接运行至少以下测试：

```powershell
& "$buildDir\Release\cod_native_vector_intent_fuser_tests.exe"
& "$buildDir\Release\cod_native_aim_dynamics_shaper_tests.exe"
& "$buildDir\Release\cod_native_controller_tests.exe"
& "$buildDir\Release\cod_native_target_coordinator_tests.exe"
& "$buildDir\Release\cod_native_bodylock_follow_controller_tests.exe"
```

- [ ] 运行注册测试：

```powershell
& $ctestExe --test-dir $buildDir -C Release --output-on-failure
```

- [ ] `git diff --check` 通过。

### Task 3B：Matched benchmark，不部署

**Artifact：**

- Create: `artifacts/benchmarks/control-chain-jump-fix-20260801/`
- Create after acceptance only:
  `docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md`

- [ ] 从当前现场 source 状态保留 matched baseline；不要把已提交 `dev` 或
  7 月 31 日 runtime 当成当前实验链 baseline。
- [ ] 复用现有 fuser-continuity、micro-input-dropout、horizontal-aim-bias 和
  sustained AimLab fixture；不要先发明新总分。
- [ ] 至少覆盖三 seed、ADS/BodyLock、pure/mixed input、普通移动目标、短 occlusion、
  full-reversal strafe。
- [ ] 记录：最大 `post_ai` 向量 delta、AI applied/rejected transitions、首个
  BodyLock `shaped/requested` 比率、overshoot area、continued push、tracking、
  false stop/interruption、manual escape。
- [ ] 可靠度摆动 fixture 的 AI off/on transition 必须从当前现场值降为 0。
- [ ] AI re-entry 单 tick delta 不得超过 `0.08`；快速释放到 manual 如使用独立
  release rate，不得超过既定 `0.20`。
- [ ] 首个 BodyLock tick 不得携带 ADS 饱和值；满足 Task 2 的 handoff 合同。
- [ ] 普通移动/混合 cohort tracking 不得回归超过约 2%；overshoot/continued push
  不得恶化超过约 5%；false stop/interruption 不得新增。
- [ ] 不得用降低 ADS/BodyLock strength 来通过 discontinuity 指标。

**Task 3 完成门：** focused tests、全量注册测试和 matched benchmark 同时通过，
且没有用力度降低换取表面平滑。

---

## 4. 条件任务：只有第一轮后仍复现，才处理观测/track discontinuity

不要在 Task 1–3 前修改 Vision、selector 或普通 tracker innovation。当前两类次级证据：

- `03.49.15.168`：同一 track 的稳定误差约 62 ms 内跳约 71 px，并以可靠度 1.0
  进入 ADS；人物运动和 body-box 几何变化同时存在。
- `03.53.15.170`：持有 LT 时 track `2635 -> 2636`，新观测相对上一 track 跳约
  95.5 px，控制保持 BodyLock。

### Task 4：最小 target admission / ownership envelope

- [ ] 先用修复后的 fuser 输出 slew 重放上述两类输入；如果最终输出已连续，停止，
  不修改 tracker。
- [ ] 若仍有拉飞，先增加 deterministic fixture：
  - same-track 70 px body-geometry shift；
  - short no-target gap 后新 track 95 px away；
  - close legitimate lateral runner 作为反例 guardrail。
- [ ] 新 track 在 held-LT 下仍不得重启强 ADS snap，但必须从 manual/零 AI 基线
  渐入 BodyLock，不能首 tick 使用完整新目标 AI。
- [ ] 不要对所有 observed innovation 加固定硬 clamp；它会伤害贴脸横移目标。
- [ ] Fresh Vision position 仍可保持 authoritative。若必须限制，只限制短期控制
  ownership/admission，不创建第二个位置状态或输出 brake。
- [ ] close moving target、jump/slide、two-candidate 场景不得出现 lazy tracking、
  错误 target hold 或更高 switch burden。

**Task 4 完成门：** 只有 deterministic observation/track fixture 仍可复现且反例
guardrail 通过，才允许保留该层改动。

---

## 5. Candidate runtime 与现场验收

- [ ] 在不覆盖当前用户 runtime 的位置构建 candidate，并记录 executable SHA-256、
  source revision + dirty diff fingerprint、config hash、engine hash、构建时间。
- [ ] 先完成 synthetic/matched acceptance，再请求用户允许替换和重启 runtime。
- [ ] 未经明确许可不得部署、重启进程或删除 runtime backup。
- [ ] 用户现场复测同类四段最后人物，重点观察：
  - ADS 初始定位是否仍瞬间拉飞；
  - ADS→BodyLock 首 30 ms 是否仍有速度突增；
  - BodyLock 在 Vision 帧间是否仍抽动；
  - held-LT 新 track 是否仍突然跳远；
  - manual escape 是否变粘。
- [ ] 新 telemetry 必须能区分 baseline/candidate runtime，不再只记录 commit 而忽略
  dirty source identity。
- [ ] 用户确认 live feel 后，才写 acceptance 文档并提出 `.agent-context/` SyncSet；
  不要把 AI 推断提前记录为 user-accepted decision。

---

## 明确非目标

- 不调低 ADS/BodyLock strength 来掩盖跳变。
- 不重新启用 BodyLock Remaining 第二位置环。
- 不把 recoil 纳入 target/Remaining 状态。
- 不增加武器数据库、额外 Vision inference、持久化学习或在线探索。
- 不重写 selector 或统一加 observation hard clamp。
- 不恢复独立 X/Y arbitration；所有 continuity 判定保持二维向量语义。
- 不覆盖、清理或提交当前无关 dirty 文件。

## 新 session 完成报告必须包含

1. 实际修改了什么，以及它在控制链中解决哪个离散边界；
2. RED 测试如何复现当前问题，GREEN 后的数值；
3. focused/full test 命令和结果；
4. matched baseline/candidate artifact 与配置身份；
5. 四类现场症状分别是否被解释；
6. 未解决项、风险和是否需要进入条件 Task 4；
7. candidate runtime hash；若未部署，明确写“未部署”。
