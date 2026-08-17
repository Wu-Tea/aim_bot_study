# Agent-First 确定性事件驱动控制架构方案

状态：Implemented candidate；offline/incident gates GREEN，等待受控 live 手感验收
日期：2026-08-15；实现更新：2026-08-16
范围：默认 native C++ Vision-to-gamepad runtime 及其测试、回放、遥测和构建合同
当前 authority：production runtime 使用直接 reducer snapshot / command / OutputComposer 路径；无 legacy runtime fallback，也无通用 event bus

## 0. 决策摘要

项目可以向事件驱动迁移，但不采用前端常见的通用 Observer 回调网络。生产控制热路径采用：

```text
固定容量 typed event buffer
  -> 固定 phase 的 deterministic dispatcher
  -> 单 owner reducer/state machine
  -> typed command/proposal
  -> 单一最终仲裁和固定输出阶段
```

Telemetry、overlay、日志和 replay 可以作为只读 Observer；任何能影响摇杆、目标、模式、开火或 recoil 的模块都不能通过异步 Observer 回调直接修改生产状态。

本方案主要优化对象不是人类阅读体验，而是 Agent 的确定性：

1. Agent 不需要阅读大段文档或猜测调用链即可定位唯一 owner。
2. Agent 能从机器可读图得到一个字段、事件或 command 的生产者、消费者、写入者和测试集合。
3. Agent 能把一次 gameplay incident 转换成稳定事件序列，并逐 transition 回放。
4. Agent 不能在错误层添加第二个状态 owner 或第二个最终输出 owner。
5. Agent 的改动影响范围和验证命令可以机械生成，而不是依赖“熟悉项目”。
6. 同一事件流必须产生确定、可哈希、可比较的状态与输出轨迹。

核心判断：**事件化离散语义，reducer 化生命周期，strategy 化判断，保留连续控制为固定周期数据流。**

### 0.1 实现收口（2026-08-17）

最初实现把 event envelope、排序 buffer、phase cursor 和 proposal mirror 加在已有同步调用链外侧。它们没有驱动 reducer，也没有被 OutputComposer 消费，只形成一套 shadow architecture。该实现已撤回，避免以后继续为影子结构打补丁。

当前生产链只保留真正拥有行为或输出的边界：

- `InputEdgeReducer -> AimScopeReducer` 直接传一次性 typed snapshot；
- target identity、R、D、ADS lifecycle 由 reducer 持有，`TargetCoordinator` 只组成 plan；
- ADS/BodyLock、dynamics 和 `AssistControlStateMachine` 产生唯一 pre-recoil T；
- AutoFire、Recoil 各自产生受限 command/contribution；
- `ControlFrame` 只携带物理输入和四个会被消费的输出 command；
- `OutputComposer` 是唯一 `GamepadOutputState` writer，并自行保证固定写入顺序。

机器合同收成单文件 `native/control_contract/architecture.json` 并参与 runtime SHA-256；不再维护一套大于实际控制实现的 schema/module/query 生成器。本文其余 event-driven 内容保留为设计背景，不等同于当前 production API。

## 1. 当前结构证据与真正的问题

2026-08-15 的源码结构扫描得到：

| 文件 | 行数 | `if` 数 | 相关头文件 bool 成员 |
|---|---:|---:|---:|
| `native/controller_native/target_coordinator.cpp` | 1392 | 99 | 32 |
| `native/runtime_app/runtime_loop.cpp` | 1303 | 69 | 10 |
| `native/controller_native/native_gamepad_controller.cpp` | 1129 | 37 | 29 |

`runtime_config.cpp` 还有约 250 个 `if`，但它主要是配置解析，不是本次控制架构问题。`if` 数本身也不是缺陷指标。真正的风险是：

- 多个 bool 隐式形成未经声明的状态笛卡尔积；
- 输入边沿和“当前是否 active”混在同一个布尔判断里；
- 一个调用既表示事实、又表示请求、又立即执行状态重置；
- freshness、target generation、FOV epoch、ADS epoch 和输出阶段各自有局部时钟；
- 只有通过阅读执行顺序才能知道某个 reset 会清除哪些状态；
- 新功能容易在 `RuntimeLoop` 或 `NativeGamepadController` 追加局部条件，从而绕开真正 owner；
- 测试常验证最终字段，却不总能证明触发事件和中间状态确实发生。

刚确认的 fire/LT 场景是代表性例子：

```text
effective_aim = physical_LT || fire_scope
```

当 fire scope 持续为 true 时，LT release/re-press 不再改变 `effective_aim`，所以 LT 上升沿的“重新定位意图”被状态判断吞掉。这个问题不是缺少一个额外 `if`，而是事件与状态没有分离。

现有项目并非完全没有事件结构：`AdsTransitionEvent`、VisionService transition sequence、telemetry event sampler、target generation、ADS epoch 和 `AssistControlStateMachine` 已经提供了局部基础。问题是它们尚未构成一个统一、机器可查询的控制合同。

## 2. Agent-first 的硬目标

### 2.1 Agent 必须能机械回答的问题

对于任何生产字段、事件、状态或 command，统一工具必须回答：

```text
owner                     唯一写 owner
producers                 允许产生事实或事件的模块
consumers                 允许读取的模块
phase                     所属执行阶段
source identities         tick / frame / target / viewport / ADS epoch
reset events              哪些事件可以重置它
reason codes              每种转移的枚举原因
telemetry projection      对应遥测字段
tests                     必跑测试和 incident fixtures
forbidden dependencies    禁止直接调用或写入的模块
```

如果工具无法回答，架构合同视为不完整；Agent 不应通过全文搜索后自行推测 owner。

### 2.2 Agent friendliness 的验收定义

一次普通控制改动应满足：

- Agent 只读取根入口、相关 module manifest、事件 schema 和目标 reducer，即可形成完整改动范围；
- 不需要加载整个 `RuntimeLoop`、`TargetCoordinator` 或历史设计文档；
- 影响测试列表由工具生成；
- 新状态写入若没有唯一 owner，会在构建前被 lint 拒绝；
- 新 event 若没有 producer、consumer、dedupe、phase 或 reason code，会被 schema 拒绝；
- 新 command 若能越过最终仲裁或 Recoil 阶段，会被依赖图拒绝；
- replay 能输出从源事件到最终 ViGEm command 的因果路径；
- 失败报告包含 event sequence 和 transition reason，而不只是 `passed=false`。

### 2.3 非目标

本方案不追求：

- 为每个概念创建抽象基类；
- 通过运行时反射或字符串 topic 获得“灵活性”；
- 把每个 1 kHz 样本包装成 domain event；
- 每个功能独立线程化；
- 允许插件直接写最终输出；
- 为了目录或类名美观进行一次性大重写；
- 用 event bus 隐藏执行顺序；
- 让 Agent 通过自然语言文档猜测机器合同。

## 3. 总体模型：事件不是样本，Observer 不是控制 owner

### 3.1 三种运行时对象

必须区分三类对象：

1. **Sample/Frame**：连续数值输入，例如 stick、目标 error、velocity、当前 plan；每 tick 或每 fresh frame 参与计算。
2. **Domain Event**：离散、不可重复解释的事实，例如 LT 上升沿、目标 generation 切换、fresh Vision commit、ADS acquisition 完成。
3. **Command/Proposal**：某 owner 希望后续阶段执行的动作，例如获取 aim lease、请求 ADS rearm、提交 aim proposal、追加 recoil contribution。

错误做法：

```text
LT=true -> 每 tick 发布 AdsPressed
error>threshold -> 每 tick begin_ads_epoch
```

正确做法：

```text
LT false->true -> 一次 PhysicalAdsPressed event
event + fresh post-FOV observation -> 一次 ReacquireDecision
decision=Rearm -> 一次 BeginAdsEpoch command
```

### 3.2 两个事件平面

生产架构分为两个平面：

#### Control plane

- controller thread 内同步执行；
- 固定容量、无堆分配；
- 固定 phase；
- 相同输入顺序必须得到相同输出；
- subscriber 是编译期登记的 reducer，不是任意 callback；
- reducer 不能直接调用另一 reducer；
- 产生的 command 只能进入声明过的后续 phase 或下一 tick。

#### Observer plane

- 接收 control trace 的不可变副本；
- 允许异步线程；
- 用于 telemetry、overlay、replay writer、performance logger；
- 不能持有或修改 production control state；
- 若观察结果需要未来控制权，必须作为一个新的、显式授权的下一 tick input event 重新进入 control plane。

经典 Observer 只允许出现在 Observer plane。

## 4. 机器可读合同是架构真源

Markdown 只解释设计；机器可读合同才是生产真源。建议新增：

```text
native/control_contract/
  schema/
    architecture.schema.json
    event.schema.json
    command.schema.json
    module.schema.json
  architecture.yaml
  events.yaml
  commands.yaml
  reason_codes.yaml
  modules/
    input_edges.module.yaml
    aim_scope.module.yaml
    vision_commit.module.yaml
    target_lifecycle.module.yaml
    target_geometry.module.yaml
    aim_mode.module.yaml
    ads_acquisition.module.yaml
    bodylock.module.yaml
    assist_arbitration.module.yaml
    autofire.module.yaml
    recoil.module.yaml
    output.module.yaml
```

生成物：

```text
native/control_generated/
  control_event.h
  control_command.h
  reason_code.h
  phase_dispatch_table.h
  telemetry_projection.h

artifacts/generated/control_contract/
  control_graph.json
  state_ownership.json
  event_catalog.json
  symbol_index.json
  context_routing.json
  impact_index.json
  test_routing.json
```

生成文件不得手改。CI/本地验证必须拒绝 schema 与生成物漂移。

### 4.1 module manifest 示例

```yaml
module: ads_reacquisition
version: 1
phase: aim_mode_transition

implementation:
  public_contract: native/controller_native/ads_reacquisition.h
  reducer: native/controller_native/ads_reacquisition.cpp
  strategy: native/controller_native/ads_reacquisition_strategy.cpp
  symbols:
    - AdsReacquisitionReducer::reduce
    - evaluate_ads_reacquisition

build_targets:
  - controller_native
  - native_ads_reacquisition_tests

context_reads:
  - target_geometry
  - aim_mode
  - vision_commit

context_excludes:
  - autofire
  - recoil
  - output

owns_state:
  - ads.reacquire_request
  - ads.acquisition_epoch

consumes_events:
  - input.physical_ads_pressed.v1
  - vision.fresh_observation_committed.v1
  - viewport.transition_committed.v1
  - target.generation_changed.v1

emits_events:
  - ads.reacquire_decided.v1
  - ads.acquisition_started.v1

emits_commands:
  - aim.begin_ads_epoch.v1

reads_snapshots:
  - target.plan
  - target.geometry
  - aim.mode

forbidden_writes:
  - output.final_stick
  - recoil.state
  - selector.identity

invariants:
  - INV-ADS-ONE-EPOCH-PER-EVENT
  - INV-ADS-FRESH-POST-VIEWPORT-EVIDENCE
  - INV-ADS-DYNAMIC-BODYLOCK-BOUNDARY

tests:
  - NativeAdsReacquisitionReducerTests
  - NativeFireLtOrderIncidentRegression
```

### 4.2 event schema 示例

```yaml
id: input.physical_ads_pressed.v1
payload: PhysicalAdsPressed
producer: input_edges
dedupe: rising_edge
delivery_phase: input_events
same_tick_consumers:
  - aim_scope
  - ads_reacquisition
required_identity:
  - controller_tick
  - input_sample_sequence
telemetry: always
```

不允许字符串 topic、运行时 subscriber 查找或未声明 consumer。

## 5. 强类型事件信封与因果身份

所有 control event 使用固定头部：

```cpp
struct ControlEventEnvelope {
    EventSequence sequence;
    ControllerTickId controller_tick;
    EventPhase phase;
    EventSource source;
    TimestampNs occurred_at_ns;
    TimestampNs ingested_at_ns;
    VisionFrameId vision_frame_id;
    TargetGeneration target_generation;
    ViewportEpoch viewport_epoch;
    AdsEpoch ads_epoch;
    ControlEventPayload payload;
};
```

不适用的 identity 使用强类型 `None`，不能用无解释的 `0`。时间、像素、stick、generation、sequence 和 epoch 不应继续以可互换的裸整数/float 表达。

推荐生成或定义：

```text
Px
PxPerSecond
Milliseconds
TimestampNs
ControllerTickId
VisionFrameId
TargetGeneration
ViewportEpoch
AdsEpoch
EventSequence
```

每个 state transition 产生固定格式记录：

```cpp
struct TransitionRecord {
    ModuleId module;
    StateCode from;
    StateCode to;
    ReasonCode reason;
    EventSequence cause_event;
    ControllerTickId tick;
    TargetGeneration target_generation;
};
```

Agent 和 replay 工具以 `cause_event` 追踪因果，不通过时间接近度猜测。

## 6. 确定性 phase 调度

`architecture.yaml` 声明唯一 phase 顺序。初始建议：

| Phase | 输入 | 允许输出 |
|---:|---|---|
| 10 `io_sample` | XInput/SDL 原始样本 | immutable physical sample |
| 20 `input_events` | 当前/前一 physical sample | input edge events |
| 30 `scope_reduce` | ADS/fire/mark edge | scope lease snapshot |
| 40 `vision_commit` | VisionService snapshot | fresh commit/no-target/target-change events |
| 50 `target_reduce` | committed observation | target identity/lifecycle/geometry snapshot |
| 60 `aim_mode_transition` | input events + target snapshot | ADS/BodyLock mode state、epoch commands |
| 70 `aim_solve` | mode + D/R + continuous samples | one bounded aim proposal |
| 80 `assist_arbitrate` | manual + aim proposal | sole pre-fire/pre-recoil final `T` |
| 90 `autofire` | physical fire + final plan | fire command |
| 100 `recoil` | effective firing state + weapon profile + recoil state | independent recoil contribution |
| 110 `output_compose` | physical passthrough + `T` + fire + recoil | one gamepad output |
| 120 `observer_publish` | immutable trace | telemetry/overlay/replay copy |

调度规则：

- phase 内 producer 顺序由 manifest 固定；
- 排序键为 `(controller_tick, phase, source_priority, event_sequence)`；
- reducer 在 phase 60 产生的 event 不能回到同 tick 的 phase 20；
- 跨回 phase 的 event 自动进入下一 tick，并在 trace 中标记 deferred；
- control event buffer 固定容量，例如 64 或由实测决定；
- buffer overflow 是硬错误和 telemetry 事件，不能静默丢弃；
- controller thread 不使用 mutex、动态分配、`std::function` 或通用 signal/slot；
- Observer plane 的背压不能阻塞 control plane。

## 7. 单 owner 状态模型

独立功能不等于独立输出 owner。建议所有权如下：

| Domain | 唯一 owner | 只允许产生 |
|---|---|---|
| 原始输入边沿 | `InputEdgeReducer` | input events |
| ADS/fire/mark 工作域 | `AimScopeLeaseReducer` | scope snapshot |
| fresh/no-target 证据提交 | `VisionCommitReducer` | committed observation events |
| selector source identity | Vision `TargetSelector` | selected observation/generation |
| controller target lifecycle | `TargetLifecycleReducer` | persistent target snapshot |
| R 与 source geometry | `TargetGeometryReducer` | geometry snapshot |
| D 与用户目标内意图 | `DesiredPointReducer` | desired point snapshot |
| `Manual/AdsAcquire/BodyLock` | `AimModeReducer` | one mode snapshot |
| ADS 生命周期 | `AdsAcquisitionReducer` | ADS proposal/commands |
| BodyLock 生命周期 | `BodyLockReducer` | BodyLock proposal |
| manual/AI 最终仲裁 | `AssistArbitrationReducer` | sole pre-recoil `T` |
| AutoFire 安全 | `AutoFireReducer` | fire command only |
| Recoil | `RecoilReducer` | recoil contribution only |
| 物理最终输出 | `OutputComposer` | sole `GamepadOutputState` |

关键不变量：

```text
TargetSelector 不决定最终 stick。
ADS/BodyLock 不直接写最终 stick。
AutoFire 不修改 aim state。
Recoil 不修改 D、R、target、ADS 或 manual/AI 仲裁状态。
Recoil 不读取 D、R、target、manual、AI proposal 或 pre-recoil `T`。
AssistArbitration 不消费 weapon profile。
只有 OutputComposer 创建 GamepadOutputState。
```

Recoil 的“独立性”通过固定 phase 和 contribution contract 保证，而不是允许 Recoil 成为第二个任意输出 owner：

```text
pre_recoil_T = AssistArbitration(...)
recoil_delta = RecoilReducer(effective_fire, weapon_profile, recoil_state)
final_right_stick = apply_recoil(pre_recoil_T, recoil_delta)
```

这里 `apply_recoil` 只属于 `OutputComposer`，负责确定性的相加、限幅和物理 stick 编码；它不能把 `pre_recoil_T` 反馈给 `RecoilReducer`。因此 recoil 始终是最后阶段的独立贡献，不会退化成“针对目标误差的补偿器”。

## 8. 用枚举状态替代 bool 笛卡尔积

互斥状态必须使用 enum/variant，不允许多个 bool 表达：

```cpp
enum class AdsLifecycle {
    Inactive,
    ArmedWaitingForFreshViewport,
    ArmedWaitingForTarget,
    AcquiringNominal,
    AcquiringExtended,
    Completed,
};
```

每个 reducer state 包含：

```cpp
struct AdsState {
    AdsLifecycle lifecycle;
    AdsEpoch epoch;
    std::optional<AdsReacquireRequest> pending_request;
    EventSequence entered_by;
    TimestampNs entered_at;
    ReasonCode last_reason;
};
```

禁止：

```text
ads_active
ads_pending
ads_complete
ads_consumed
ads_waiting
```

同时存在并依靠注释说明合法组合。

合法 transition 必须在 manifest 或生成 transition table 中声明；未声明 transition 在 debug/test 中硬失败，在 Release 中 fail closed 并输出 reason code。

## 9. CQB fire + LT 作为首个竖切面

### 9.1 事件合同

首个迁移竖切面只包含：

```text
input.physical_ads_pressed.v1
input.physical_ads_released.v1
input.manual_fire_pressed.v1
input.manual_fire_released.v1
scope.lease_acquired.v1
scope.lease_released.v1
vision.fresh_observation_committed.v1
viewport.transition_committed.v1
ads.reacquire_requested.v1
ads.reacquire_decided.v1
ads.acquisition_started.v1
```

### 9.2 Aim scope lease

```text
PhysicalAdsLease
ManualFireAimLease
EnemyMarkVisionLease
```

`effective_aim_scope` 由 lease set 派生，但输入事件不从派生 bool 反推。因此：

```text
保持 fire
-> ManualFireAimLease 仍在
-> LT release 不关闭 Vision/AI 工作域
-> LT re-press 仍产生独立 PhysicalAdsPressed event
```

### 9.3 Reacquire 事件与判断分离

`PhysicalAdsPressed` 无条件产生 `AdsReacquireRequested`。事件不能立即调用 `begin_ads_epoch()`，而是等待与新 viewport epoch 对应的 fresh observation。

判断 strategy：

```text
event exists
AND post-event viewport/FOV evidence is current
AND current target generation is valid
AND current mode is not already active AdsAcquire
AND target error exceeds dynamic BodyLock continuation envelope
=> BeginAdsEpoch
```

100px 不成为新常量。ADS 和 BodyLock 共用一个生成/纯函数：

```cpp
Px target_scaled_radius(Px base, NormalizedTargetSize size);
```

当前等价形式：

```text
base * (1 + 0.75 * normalized_target_size)
```

Reacquire threshold 使用动态 BodyLock continuation 半径，因为判断问题是“当前偏差是否已经明显超出 BodyLock 能力范围”；ADS effective radius 继续用于 ADS response/demand。共享 helper 防止 Agent 在两个 owner 中复制 `0.75` 或新写 `100.0f`。

### 9.4 明确决策原因

至少需要：

```text
RearmedOutsideBodylockEnvelope
KeptBodylockWithinEnvelope
CoveredByActiveAdsSnap
DeferredWaitingForFreshViewport
DeferredWaitingForTarget
CancelledByTargetGenerationChange
CancelledByAdsRelease
ExpiredWithoutEligibleObservation
```

### 9.5 必须 GREEN 的事件序列

1. `fire held -> LT off/on -> error outside dynamic envelope`：只增加一次 ADS epoch。
2. 同序列、error inside envelope：epoch 不变，继续 BodyLock。
3. 当前仍是 AdsAcquire：事件消费为 `CoveredByActiveAdsSnap`，不重置 sticky snap。
4. 只有 pre-FOV fresh frame：不得 rearm。
5. target generation 在事件后切换：旧事件不得作用于新目标。
6. LT 持续 held：只产生一个 rising event。
7. 远/小目标和近/大目标：阈值使用同一个 target-size scaling contract。
8. fire lease 保持时 LT release：工作域不中断，LT 输出仍物理透传。
9. 无目标：事件可以短暂等待目标，但必须绑定 viewport epoch 和有界生命周期。

## 10. Agent 查询与改动协议

当前不再维护大于实现本身的查询/生成工具。Agent 的稳定入口是单文件
`native/control_contract/architecture.json`，其中给出生产 flow、唯一 state owner、
实际 snapshot/command 和禁止结构。

每次生产改动只执行以下协议：

1. 从该合同确认唯一 owner 和直接上下游；
2. 读取 owner 实现、直接调用点与对应 CMake 测试，不加载整套历史设计；
3. 将真实 incident 固化为 symptom oracle；
4. 只修改唯一 owner，不创建 mirror state、shadow event 或 alternate output；
5. 运行 owner unit、生产链 regression 和完整 Release CTest；
6. 若 owner/flow 变化，同步单文件合同并由 runtime 记录新 hash；
7. 删除被替代实现，不保留可重新启用的 legacy 分支。

Agent 不允许因为“附近已经有一个 if”就把功能放入附近文件。落点由 manifest owner 决定。

## 11. 自动验证工具（历史提案，未采用）

最低工具集：

### 11.1 `control_contract_lint`

验证：

- schema 完整；
- event ID/version 唯一；
- 每个 state path 恰有一个写 owner；
- producer/consumer 存在；
- phase 合法；
- dependency graph 无环；
- command 不逆 phase；
- reason code 被 telemetry 和测试引用；
- module 声明的测试目标存在。

### 11.2 `control_state_write_scan`

结合生成标记、AST/clang tooling 或受限宏，拒绝跨 module 写 owned state。纯文本扫描只能作为早期实现，不作为最终证明。

### 11.3 `control_trace_replay`

输入：标准事件流和连续 sample frames。输出：

```text
events.jsonl
transitions.jsonl
commands.jsonl
plans.jsonl
outputs.jsonl
summary.json
```

同一输入、同一 runtime/source identity 的输出必须字节稳定或经过明确浮点规范化后哈希稳定。

### 11.4 `control_impact`

从 module manifest 和 event graph 计算：

```text
直接消费者
下游 commands
最终输出路径
相关 telemetry
必跑 unit/contract/replay/live gates
禁止触碰的 owner
```

### 11.5 `control_explain`

给定 tick 或 output sequence，生成机器因果链：

```text
source input/vision event
-> reducer transition
-> reason code
-> command/proposal
-> arbitration decision
-> recoil contribution
-> final output
```

不得只输出自然语言总结；自然语言由 Agent 基于结构化记录生成。

## 12. 性能和确定性预算

事件架构不得降低实时性。硬门禁建议：

- controller tick 热路径零 heap allocation；
- control plane 零 mutex；
- event payload 固定上限并可静态断言；
- control buffer 无静默 drop；
- dispatcher 时间 P95/P99 进入 runtime performance telemetry；
- Observer plane 慢或崩溃不影响 output compose；
- 同一 tick 内 phase 顺序稳定；
- 无递归 dispatch；
- 无 subscriber 在 callback 中直接触发另一个 subscriber；
- Release source identity、event schema version 和 architecture hash 写入 session manifest。

应新增：

```text
control_contract_hash
event_schema_version
architecture_version
control_event_count
control_event_buffer_high_watermark
control_event_drop_count
dispatcher_us
transition_count
unhandled_event_count
```

`unhandled_event_count` 和 control event drop 在正式验证中必须为 0。

## 13. 测试结构

### 13.1 Reducer unit

输入一串 typed events，断言：

- 最终 state；
- transition 序列；
- reason codes；
- emitted commands；
- 无未消费事件。

测试不需要启动完整 RuntimeLoop。

### 13.2 Module contract

验证 manifest 与 C++：

- consumed/emitted event 集合一致；
- state ownership 一致；
- reason code 完整；
- phase 不越界。

### 13.3 Production-chain replay

使用真实 dispatcher、reducers、strategies、arbiter、recoil 和 output composer。禁止测试专用算法绕过 production owner。

### 13.4 Incident regression

Gameplay incident 固化为：

```text
source evidence
event stream
continuous sample stream
trigger assertions
symptom oracles
counterfactuals
known-bad hash
candidate hash
```

### 13.5 Live A/B

仅验证 offline 无法表达的 FOV、game response、capture timing 和主观手感。Live 不能替代 reducer/trace gate。

## 14. 迁移方案

禁止 big-bang rewrite。每阶段保持当前 production authority，直到 shadow trace 与事件合同通过。

### M0：生成当前 control graph（已完成，canonical contract 已继续升级到 v2）

目标：不改行为，把现有 owner、调用边和测试登记到 manifest。

交付：

- schema；
- architecture/module manifests；
- owner/impact/test 查询工具；
- 当前 graph lint；
- 明确记录无法证明唯一 owner 的冲突点。

退出条件：Agent 能从 manifest 还原当前 production chain，且与源调用审计一致。

历史 M0 曾只登记旧 module/state/call/test/reason-code 和 5 个冲突；该快照已被 production v2 contract 替代。当前 JSON contract 同时登记 module、state、event、command、phase、reason code 和测试路由，不再把已经删除的旧 owner 描述成现状。

### M1：Shadow event envelope（已完成并提升为 production ControlFrame）

目标：在现有 RuntimeLoop 周围生成 input/vision/target/mode/output shadow events，不参与控制。

退出条件：

- event trace 与现有 telemetry 对齐；
- sequence、tick、frame、generation、viewport identity 完整；
- event logging off/on 不改变输出；
- 热路径预算通过。

### M2：Input edges + AimScope leases（已完成）

目标：替换分散的 previous-button bool 和 effective aim OR，但保持最终行为。

首个竖切面：fire/LT CQB。

退出条件：

- fire activation 已有目标/无目标回归保持 GREEN；
- LT re-press event 不被 fire lease 遮蔽；
- scope lease trace 无泄漏；
- 旧 edge bool 被删除。

### M3：ADS/BodyLock lifecycle reducers（已完成）

目标：从 `TargetCoordinator` 提取显式 lifecycle，不改变 TargetPlan 语义。

退出条件：

- 当前 ADS、BodyLock、far selected person、high-frequency BodyLock、FOV transition fixtures 全部 matched；
- state transition table 覆盖 100%；
- `TargetCoordinator` 不再拥有 ADS bool 集合。

### M4：Target lifecycle/geometry split（已完成）

目标：把 identity/lifecycle、R/source geometry、D/user desired point 分离为单 owner snapshot。

退出条件：

- target generation 和 desired point 的每次改变都有 cause event；
- selector 与 controller persistent identity 边界可查询；
- fresh no-target 和 no-new-frame 不再共享路径。

### M5：Proposal/command pipeline（已完成）

目标：ADS、BodyLock、AutoFire、Recoil 只输出声明过的 proposal/command；OutputComposer 成为唯一 `GamepadOutputState` writer。

退出条件：

- state-write scan 证明唯一 writer；
- Recoil phase 独立性回归 GREEN；
- physical passthrough 与 AutoFire/Recoil 顺序有显式合同。

### M6：删除 legacy orchestration（offline gate 已完成；live acceptance 待用户实测）

目标：删除已被 reducer/dispatcher 替换的 if、bool、直接调用、配置别名和测试专用分支。

退出条件：

- 无双 owner；
- manifest 与生成图无 legacy 节点；
- full Release、全部 incident contracts、matched replay、性能门禁通过；
- 一次受控 live session 通过后再接受生产切换。

## 15. 每阶段禁止事项

- 不允许新旧控制 owner 长期并存并由 config 切换。
- shadow 组件不能拥有 actuation authority。
- 不允许 generic pub/sub 框架进入 controller hot path。
- 不允许事件 payload 使用 `std::any`、字符串 map 或未定界 JSON。
- 不允许 Observer 修改 reducer state。
- 不允许为迁移保留第二套 final stick。
- 不允许把旧 bool 包进 event payload 后宣称完成事件化。
- 不允许先拆文件、后补 owner contract。
- 不允许以全套 CTest 通过代替事件序列和 matched trace 证明。
- 不允许用新 magic threshold 代替共享 strategy。

## 16. Rejected alternatives

### 通用前端式 Observer/EventEmitter

拒绝。它隐藏 consumer 顺序、允许重入、增加生命周期问题，并使控制结果依赖订阅关系而不是显式 phase。

### 每个功能一个 actor/thread

拒绝。目标、ADS、BodyLock、仲裁和 output 需要同 tick 的确定顺序；线程化会引入消息延迟和难以复现的 race。

### 所有 tick/sample 都事件溯源

拒绝。连续 stick 和 target 数值是数据流，不是稀疏 domain event。全量事件化会扩大 trace、增加分配压力并模糊真正 transition。

### 一次性重写 Controller V2

拒绝。当前已有 live 体感收益和多套 incident regression；大重写无法把行为差异归因到单一 owner。

### 只把大文件拆小

拒绝。没有 machine-readable owner、event 和 phase contract 时，拆文件只会把隐式状态分散到更多位置。

### 让每个功能直接写 output，再由 mixer 相加

拒绝。它恢复已经删除的多输出 owner 和 manual+AI+recoil 隐式叠加问题。

## 17. 第一批实际交付物（历史提案，已由 0.1 的精简实现取代）

若方案获准，第一批不应直接改算法，而应交付：

1. `native/control_contract/schema/*.json`；
2. 当前架构的 `architecture.yaml` 和 module manifests；
3. `tools/control_contract.py owner|event|impact|tests|graph`；
4. 生成的 `control_graph.json`、`state_ownership.json` 和 `test_routing.json`；
5. graph lint 与 generated-drift CTest；
6. fire/LT 竖切面的 shadow event catalog；
7. 当前实现与 shadow event 的一致性报告；
8. 不改变生产输出的性能报告。

只有这些入口稳定后，才进入 M2 的生产 edge/lease 替换。

## 18. 方案验收标准

架构不能因“使用了事件”而被接受。至少满足：

- 任何生产 state path 都能查询唯一 writer；
- 任何 control event 都有唯一 producer、明确 dedupe 和固定 phase；
- 任何最终输出都能追溯到源事件/样本和 transition reason；
- 相同输入 event/sample stream 产生稳定 trace hash；
- controller hot path 无动态分配、无锁、无异步 callback；
- Observer plane 无法写 control state；
- Agent 可以在不读取大文件的情况下得到影响图和必跑测试；
- CQB fire/LT 事件序列满足动态 BodyLock envelope 合同；
- Target-first 单输出 owner、Recoil 最后独立阶段、fresh/no-new-frame 区分全部保留；
- 每次 authority 切换都有明确、可枚举、可测试的 reason code；
- 旧 owner 在新 owner 获得 authority 后被删除，而不是永久保留为 fallback。

达到以上条件后，项目才算从“条件分支驱动的隐式状态机”迁移为“对 Agent 可查询、可生成、可回放的确定性事件架构”。
