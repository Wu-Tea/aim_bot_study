# Agent Session Log

Last compacted: 2026-09-01
Scope: native C++ FPS runtime, Vision/Controller ownership, Fusion Canvas,
capture isolation, native gates and runtime operations.

## How To Use This File

- Read the newest checkpoint and `handoff.md` first.
- August 3-18 detail: [pre-September archive](archive/session-log-2026-08-03-to-2026-08-18-pre-20260901-compaction.md).
- August 1-3 detail: [August archive](archive/session-log-2026-08-01-to-2026-08-03-pre-20260803-compaction.md).
- July 23-31 detail: [July archive](archive/session-log-2026-07-23-to-2026-07-31-pre-20260801-compaction.md).
- Earlier history: [pre-July-20 context](archive/context-through-2026-07-20-pre-compaction.md) and `session-log-full.md`.
- **User-confirmed**, **Repository evidence** and **Inferred/open** retain their
  literal meanings; do not promote an inference into a fact.

## 2026-09-01 - Native-Only Branch and Fusion Recovery

- **User-confirmed scope:** create a clean native-runtime branch, keep local
  model/config untracked, compile it, repair Fusion on clean and dev, then sync
  context and commit.
- `codex/runtime-clean` tracks only the C++ runtime, Fusion, native contracts,
  launch/build scripts and current required docs. Machine-local assets remain ignored.
- Both failed Canvas runs resolved clean-worktree paths, passed the production
  DXGI preflight, connected IPC and entered `running`; absolute repository
  paths were not the owner.
- **Root cause:** display and DWM notifications shared one invalidation flag and
  were treated as terminal proof loss instead of a request to hide and revalidate.
- **Fix:** Canvas now enters `RevalidationPending`, hides, logs the exact
  reason, refreshes geometry, reapplies/readbacks exclusion, reruns the
  production `DxgiRoiCapture` probe and resumes only on full proof. Failure,
  ambiguity or another change during proof remains terminal fail-closed.
- The recovery probe cannot post `WM_QUIT` to production; renderer resize
  reports failure and releases replaced graphics references. The launcher uses
  its current PowerShell host and verifies liveness before publishing state.
- **RED/GREEN:** the new lifecycle fixture first failed, then passed. Injected
  `WM_DISPLAYCHANGE` and `WM_DWMCOMPOSITIONCHANGED` each completed real DXGI
  revalidation while Canvas/native stayed alive.
- **Verification:** clean full Release build and `11/11` suites PASS; dev
  Fusion build, `26/26` CTest and DXGI preflight PASS. Test processes were stopped.
- Commits: clean creation `e7d8840`; repairs `62e3604`
  (`codex/runtime-clean`) and `bd462f3` (`dev`). Repaired blobs are identical.
- **Inferred/open:** the old combined log cannot prove which notification caused
  the original exits. Real MW4 mode changes and physical topology resize remain live gates.
- No subagents were used. Raw logs, PIDs, SDK paths, binaries, secrets, local
  asset details and unrelated research were excluded.
- SyncSet `sync-20260901-001`, reviewer `accept_draft`: updated handoff and the
  accepted Fusion decision; archived the prior 563-line log before compaction.
- Follow-up: run the clean launcher in normal MW4 mode and exercise one display transition.

## Current Active Checkpoints

- **Fusion:** deterministic recovery is GREEN; real MW4 presentation changes and
  physical monitor resize remain live acceptance boundaries. Never weaken capture
  isolation to improve liveness.
- **BodyLock:** the August 18 capture-aligned observer moved the frozen step from
  no response within 900 ms and `21.040884 px` growth to 23 ms and
  `2.698075 px`, with zero recovery overshoot. Live recoil/FOV/noise feel and
  the high online response estimate remain unverified.

## Durable Milestone Index

- 2026-06-25: accepted performance-first, visual-only Fusion with no gameplay authority.
- 2026-08-01 to 03: established one final output and protected the live baseline.
- 2026-08-05: accepted fixed-shape TensorRT/CUDA Graph throughput optimization.
- 2026-08-07: clarified target-first `T`; manual is evidence, not a protected force.
- 2026-08-10: retired the low-rate legacy stack and duplicate owners.
- 2026-08-11: implemented I/R/D/T ownership and incident-first validation.
- 2026-08-12: granted full ADS authority after admission and completed auto-mark.
- 2026-08-16: bounded latency test found no material controller-internal queue.
- 2026-08-17: repaired BodyLock axis reversal and retired Direct/PID production.
- 2026-08-18: implemented capture-aligned BodyLock sustaining demand; live gate open.

## Current Architecture and Maintenance

```text
Vision -> selector -> TargetCoordinator -> TargetPlan
       -> ADS acquisition OR BodyLock follow
       -> AimDynamicsShaper -> AssistControlStateMachine
       -> AutoFire -> independent recoil -> virtual gamepad

Vision -> latest-only Fusion channel -> visual-only Canvas
                                      -> fail-closed DXGI exclusion lifecycle
```

One owner per identity, lifecycle, state and final-output decision. Fusion never
owns gameplay control. Fresh Vision owns position. Aggregate benchmark scores do
not replace incident/product gates. Keep secrets, personal media, raw telemetry
and machine-local runtime assets out of project context.

## 2026-09-07 — Bounded target addressing

- User authorized geometry-only target addressing, followed by a scoped commit and documentation; another session is independently optimizing Vision.
- DesiredPointReducer now chooses a bounded upper point once on fresh ADS admission and retains its normalized position across fresh/cue updates and ADS→BodyLock. Source association and motion geometry remain Vision-owned.
- Standard-box selection is bounded to approximately 40%→36.4% height, with a central approach corridor and two arrival radii reserved for braking. Manual correction wins; replacement resets offsets.
- Frozen RED→GREEN fixture: extra downward demand 7.2→0 px. Final 10 Base/Feature suites, 365 cases PASS; current workspace launcher runtime rebuilt. An intermediate telemetry rotation test failed intermittently and passed on the final run; its cause is unresolved.
- Live feel and silhouette correctness remain unverified. No claim of neck recognition, FPS improvement, or live acceptance.
- Implementation and reproduction: `docs/project/TARGET_ADDRESSING_GEOMETRY_20260907.md`; commit subject: `fix(controller): retain bounded approach-selected target points`.
- Scope excludes Vision source/configuration, its experiment document, models, shared build changes, and other worktrees. No handoff or existing decision was rewritten.
- SyncSet reviewer: `accept_draft`; user-authorized documentation, factual results separated from unverified live outcomes; raw media, telemetry and secrets excluded.

## 2026-09-26 — Gamepad multiplayer improvement checkpoint

- **User-confirmed:** ordinary multiplayer/normal aim assist no longer showed felt jitter or oscillation after the requested build; user authorized recording, removing unimportant generated byproducts and committing.
- Retained changes: 15–30% intent/authority curve without raw deadzone, final DS4 delivery receipts and failure boundaries for camera work, detector confidence separate from ranking, and accumulation of short source intervals for high-rate observation/learning. Arbitration implementation moved out of the public header. Rejected calibration/prediction experiments were restored before the tested build.
- **Verification:** 410 product tests pass; high-rate RED→GREEN contract complete; 864 fixed opportunities show no new protected regressions against the preceding continuous-intent worktree, and 1,944 development random cases match. Existing synthetic mismatch RED remains failing; no global optimization acceptance.
- **Bounded audit:** exact runtime/config hashes match the prepared build. Two intact rotations cover 658 seconds; 61,766 unique controller samples and 142,981 delivered samples contain no delivery failure, nonfinite axes or output overflow. Event-ring repeats/out-of-order emission were handled by exact producer sample sequence, with no ambiguous cross-stream tick join.
- **Inferred/open:** one 217 ms reversal signature and one ADS-extension output jump remain saved observations; no independent motion truth proves their cause. Last log rotation has a truncated row, session did not cleanly close, game refresh/hardware profile and matched baseline are unavailable. Audit remains INSUFFICIENT_EVIDENCE for full live acceptance. Strong-AA modes are not covered by the user's normal-multiplayer confirmation.
- Removed only a superseded early scan CSV and a generated probe object (87.70 MiB); latest raw logs, RED/GREEN evidence, final matrix reports and runtime backup retained. Unrelated workspace work excluded from the commit.
- Record: `docs/benchmarks/CONTROLLER_MULTIPLAYER_CHECKPOINT_20260926.md`; source/build/audit hashes remain in its referenced local artifacts.
- SyncSet reviewer: `accept_draft`; direct user authorization covers recording, prior handoff archived, no raw telemetry/personal machine paths copied into context, no new global acceptance decision. No subagents used.


## 2026-10-04 — Isolated C++ cognition refactor completed

- User authorized completion in a new worktree without repeated questions. Branch `codex/cognition-refactor-20261004`; inherited source/doc snapshot `1251273` preserves original concurrent changes. Original workspace untouched; output media excluded from the snapshot.
- Read project-cognition method and existing coordination review; updated existing project model instead of introducing a new knowledge framework. Concentrated IPC implementation/reload transitions, Loop control versus diagnostics, shared telemetry shapes and Fusion conversion; removed repeated derived state and field mapping. Kept reasonable controller/Vision/mouse ownership boundaries and actual Python dependencies.
- RED-to-GREEN boundary fixes: malformed scalar configuration is rejected rather than silently defaulted/prefix-parsed; output disabled does not construct a virtual gamepad. No production selector/control algorithm change; one obsolete Python selector expectation aligned with the existing native flat-person identity contract.
- Fresh Release full build, CTest 50/50 groups and affected Python tests 134/134 pass. Actual output-disabled TensorRT runtime completes control-value and vision-policy reloads via Python IPC/Loop and named-event stop, with current telemetry serialized. Device receiver, gameplay, Fusion window and independent review remain unverified.
- Scope/details/source/verification: `docs/project/CURRENT_STATE.md`. Old SHA256 validation and benchmark acceptance framework remain retired; retained simulation tests do not constitute a release verdict.
- SyncSet reviewer: accept_draft (self-review); user's current one-pass authorization covers this worktree handoff/milestone update. No decision rewrite, raw telemetry, personal captures or secrets included. No subagents or cross-chat confirmation claimed.


## 2026-10-04 — Test cleanup and C++-only authorization

- User authorized deletion/consolidation of old tests, emphasizing pre-August scenarios. Removed 74 Python test modules, 29 native historical incident drivers and 14 test-only fixtures; removed 13 source-shape assertions and migrated current product authority/manual-correction assertions into native base contracts.
- Release all-target build and CTest 50/50 groups pass. Remaining Python full collection was 270 pass/1 skip before removing 13 static assertions; affected startup 18 pass and final default basic set 123 pass, full collection now 258. Old detailed fallback/research/incident coverage is intentionally retired, not equivalent coverage.
- Latest user explicitly authorizes eliminating Python runtime/build/tool participation and dependencies, with useful capabilities implemented in C++. This supersedes earlier retention boundaries. Migration is in progress; GUI/config/start-stop/hot-reload/learning display are selected useful capabilities.
- SyncSet self-review accept_draft under continuing one-pass authorization: current scope and completed checks only; no secrets/raw logs/decision rewrites. Original workspace remains untouched.

## 2026-10-04 — C++-only migration completed

- User explicitly authorized retirement of every Python path and dependency. Removed remaining Python runtime, GUI, tests, training, research/analysis tools and pybind extension. Selected desktop config edit/validate/save, verified start/stop, reload/state/learning and logs now use native C++; old graphical curve editor and preset library are retired, user assets and native curve semantics preserved.
- Native DesktopSession owns configuration bytes/conflict detection and PID+creation-time+normalized executable identity. Win32 window delegates commands; existing IPC ABI extracted into one shared header. Suspended child startup records ownership before running; shutdown uses the runtime event, no process-name force kill. SDK SDL/ViGEm inputs now come from native runtime_deps or explicit build parameters, not site-packages.
- Fresh Python-free CMake configure and full Release build pass. Final CTest 51/51 groups pass including numerical simulation and hidden window. Real output-disabled native TensorRT start/control reload/vision-policy reload/stop pass; learning preserved then cleared at appropriate commits. Standalone native vision completes three real captured/inferred frames. PowerShell AST and background start/stop/Fusion previews pass.
- Malformed config-line RED regression corrected at native loader; Windows executable-path ownership mismatch reproduced and corrected at native session normalization. No controller algorithm change. Native config editor uses current loader subset; general TOML editing is not promised.
- Physical input/output/gameplay, Canvas exclusion and full manual GUI use remain unverified. No ONNX available for real trtexec export. Automatic approval rejected recursive ignored Python/pytest cache cleanup with blocked by policy; no workaround attempted. These historical ignored outputs do not participate in current paths.
- Source model and docs updated in target worktree only. SyncSet self-review accept_draft under ongoing one-pass authorization; original workspace untouched, no decision rewrites, no external coordination confirmation claimed.

## 2026-10-04 — Merge into dev while preserving dev GUI

- User requested direct dev merge, excluding new GUI and retaining current dev version. Saved existing uncommitted cleanup/source state as 05d1c83, then merged refactor ancestry. Kept dev GUI/config workspace/curves/runtime/IPC, VBS, GUI docs/tests and background start/stop/Fusion scripts exactly unchanged. Excluded all new native desktop sources/target/tests. Retained native independent-game profile support from dev.
- Runtime/diagnostics/state ownership refactor, historical test reduction, native DLL build inputs and non-GUI Python/pybind retirement are integrated. Python UI exception supersedes blanket retirement; native control/Vision/build still have no Python participation. User configs/models/profiles and original untracked output artifacts untouched.
- Full merged Release build passes; CTest 50/50 groups and GUI unittest 66/66 pass. Actual retained Python RuntimeManager/ControlChannel starts merged TensorRT runtime with output disabled, applies control reload revision 0→1, and gracefully stops exit=0. GUI source/launch scripts have zero diff from pre-merge dev. No controller algorithm change or new live gameplay acceptance.
- SyncSet accept_draft self-review under direct merge and ongoing one-pass authorization. Updated current entry/model/handoff to reflect Python GUI exception; no new decisions, raw logs, secrets or writes to project-cognition.


## 2026-10-06 — Desktop, parameter semantics and tracking checkpoint

- User authorized redesign across GUI/config/native semantics, independent caps and response times, all editable values matching actual accepted ranges, 3% AI-only deadzone, and up to 32 ms same-target miss continuity. Later authorized autonomous curve tuning and recording/committing the entire workspace.
- Redesigned retained Tk workspace and curve/range examples; generated numeric parameter catalog; stable SDL device identity/reconnect checks, input activation/release handling, reload boundaries and frame-rate reporting included in the workspace checkpoint.
- Removed hidden recoil floor and numeric mismatches; strict configuration boundaries and explicit old-key migration preserve user assets. Config output end markers reject incomplete IPC output; the intermittent truncation origin remains unproven.
- COD timed-wait scheduling defect corrected; sustained-ADS measured capture/inference roughly 66–69 → 166–167 FPS at configured 180. Game rendering FPS was not the reported issue.
- Supersedes today's intermediate hard point-stop/floor design: continuous position braking (nominal 25 ms), independent sustaining motion, five capture intervals / maximum 25 ms displacement-based velocity estimate. GUI now labels arrival radius as 近点收尾半径; preview protocol v3. No optical-flow or new trajectory extrapolator claimed.
- Final Release + 50 CTest groups + 111 desktop tests pass. 192 simulated scenarios / 1,344,000 steps; 90/96 paired configured-floor cases reduce mean error. Six stationary residual-error regressions explicitly retained. Latest Apex live acceptance still pending.
- Navigation/evidence: `docs/project/CONFIGURATION_AND_CHECKS.md`, `output/README.md`, `decisions/DEC-2026-10-06-001-assist-parameter-and-continuity-semantics.md`. Prior handoff archived. Output includes historical intermediate failures, not all final verdicts.
- SyncSet reviewer accept_draft under explicit user authorization. No personal profiles/models or ignored build/raw telemetry force-added; no subagents or external push requested.


## 2026-10-12 — DSE directional notifications and WebView2-only GUI

- **User-confirmed:** accepted left/both/right DSE pulse feel, requested notification enable/strength controls, authorized deletion of old Tk GUI, and finally requested recording and committing this conversation's decisions/changes.
- **Implemented:** native USB DSE WASAPI worker and fresh-person occupancy policy; default off, 24% strength, 100 ms directional pulses, hot reload without resetting controller learning. Actual WebView2 参数调校 → 目标与识别 contains settings. Tk window/widgets/dedicated tools/tests removed; shared Python services and user assets preserved.
- **Verified before commit preparation:** frontend/native builds, four Feature suites, 79 desktop tests, actual WebView2 persistence/close protection and default/minimum layout. DSE worker playback/disable/shutdown passed. Full live-game automatic notification behavior remains unverified; obsolete Tk tests are not claimed as equivalent replacement coverage.
- **Commit isolation:** only the haptics hunks in shared runtime-loop files are staged. A HEAD-based native source snapshot with exactly the staged native/config changes independently builds Release runtime and functional tests; all four Feature suites pass. This establishes build independence from concurrent controller/relay/telemetry work.
- [Decision and implementation boundaries](decisions/DEC-2026-10-12-001-dse-haptics-and-webview-gui.md). Other concurrent controller/Vision/telemetry changes are outside this commit; raw output and personal files remain local.
- SyncSet `sync-20261012-002`: accept_draft self-review under explicit record/commit authorization. No inferred user acceptance, secrets, raw personal data, subagents or external writes. Historical log retained; separate compaction remains suggested.

## 2026-10-16 — DEV checkpoint and clean optimization sync

- User paused clean deletion and requested DEV checkpoint first, then optimization synchronization. `6496f84` preserves long capture/GUI/Vision diagnostics; the separate sync uses deletion-before archive `5eb02ff` and retains DEV tools/tests/mouse/React GUI.
- Before 50/50 and after 51/51 CTest groups pass; original DEV replay records match for 192 scenarios / 676,544 ticks, as do clean's detailed replay records. Full build, 80 desktop tests, 10 capture tests, actual WebView2 and output-disabled engine smoke pass. Gameplay acceptance remains unverified; see [configuration and checks](../docs/project/CONFIGURATION_AND_CHECKS.md).
- Clean's uncommitted cleanup stays paused. No output recordings committed and no remote push. Default DEV runtime rebuilt after user stopped the running process.

## 2026-10-16 — Recreated clean from DEV with the complete GUI

- **User-confirmed:** archive old clean first; rebuild from current DEV; retain full desktop GUI/runtime/fixed recoil/dependencies; keep tests and experiments in DEV. Old clean is `ca0ea11` / `codex/runtime-clean-archive-20261016` with a separate worktree and copied/verified local assets. New clean is `ba679ce`, tag `codex/clean-baseline-20261016`, based on DEV `aa0f918`.
- **Verified:** fresh native/frontend builds; 448 native cases linked to clean libraries, 80 desktop and 10 capture cases; seven replay tables identical to DEV; real GUI start/engine load/Applied hot reload/normal stop; Fusion attach/close without stopping runtime. Output and haptics disabled during live smoke; no gameplay claim. Evidence/harness in `runs/clean-recreate-20261016/`, not in clean.
- **Boundaries:** 232 tracked product files, no test/experiment tree; all retained native/GUI source files match DEV. GUI means React/WebView2, not Fusion. Local profiles/models retained and missing referenced engine copied; no remote push. Future major changes use a temporary branch from the baseline.

## 2026-10-16 — clean 身体框与短缺帧修复，等待实机复测

- **用户授权：** 从 clean 临时分支修复两个问题，保留手动权与观测时效，完成场景回归和独立种子短长仿真后合回；随后明确要求同步上下文，准备实机复测。
- **产品事实：** `739bc3d` 从 `codex/clean-incident-fix-20261016` 快进合入 `codex/runtime-clean`，native/React 已重建。去除身体框高宽比 0.65–0.85 准入空档；身份保留与预测时限分离。DEV `07ae3e8` 仅保存夹具，生产修复没有同步到 DEV 生产代码。
- **边界：** 原 capture 年龄不续期；预测最多 32 ms，超过 32 ms 且未到 40 ms 仅保留身份/未完成 ADS，辅助瞄准与开火权为零，达到 40 ms 释放。手动权、原始透传、源瞄点、曲线和输出上限保留。见[决策](decisions/DEC-2026-10-16-001-clean-gap-identity-and-prediction.md)。
- **验证：** 原版 3 组回归失败、修复版通过；448 原生、80 GUI 均通过；4 种子、128 个 2/20 秒场景、1,408,000 tick 通过。开发 20261016/730201，独立验证 982451/416103。真实 GUI 启动/Applied 热更新/IPC/停止在临时和最终 clean 均通过，输出/震动关闭。
- **证据与限制：** 会话 `20261016T123101Z_39880_1`、最后视频 `Replay 2026-10-16 20-35-04.mp4` 的框尺寸和状态时序进入回归，没有重新对视频逐帧推理。记录在 `runs/clean-incident-fix-20261016/RESULTS.md`，假设在 `tests/clean_incident_20261016/README.md`。完全漏检、尺寸不足和手动反向保护仍可能产生相似观感；实机验收待用户反馈。
- SyncSet `sync-20261016-001` 审阅通过（accept_draft），依据明确写入授权；无推断验收、无原始视频/日志复制、无外部写入。历史保留，日志压缩另行建议。

## 2026-10-18 — clean 同步 DEV 与闭环检查

- 用户要求同步到 DEV、做闭环检查并探索优化方向；`b0269f0` 同步 clean `739bc3d` 的四个生产文件，DEV 测试直接拥有 32 ms 预测/来源硬时限断言，移除生成副本替换步骤。
- 构建、51 CTest 组、80 GUI、10 采集测试及关闭输出的真实 GUI/runtime 启停热更新通过；4 新种子、128 场景的 DEV/clean CSV 记录一致，7 份既有回放一致。0/6/12 ms 三档共 384 场景、4,224,000 tick 边界检查通过。
- 探针发现固定框高度 64.8→65.2 px（宽 100）跨 0.65 时源瞄点跳 29.08 px、Y 输出反向；+12 ms 延迟使 93/128 合成案例误差变大。均为待研究线索，不是实机根因或已实施优化。详见 `docs/project/DEV_CLEAN_SYNC_20261018.md`、`runs/dev-clean-sync-20261018/`。
- SyncSet `sync-20261018-001`：accept_draft，沿用用户对本次修复/验证的上下文写入授权；修正 DEV 未同步的陈旧状态，不推断实机验收。历史日志保留，后续建议单独压缩归档。

## 2026-10-18 — 0.65 胸部参考连续性修复并同步 clean

- 用户要求先在 DEV 修复、回归和独立种子短长闭环验证，再同步 clean 编译，保留胸部/手动权/32–40 ms 且不用延迟开关。DEV `d10dc97`，临时 clean 分支 `codex/geometry-continuity-20261018` 提交 `866bd9c` 已快进合入产品。
- 源几何空间 smoothstep 混合已有胸部与区域参考，GUI 预览 v4；固定框探针跳变 29.08→0.081 px。旧版连续性回归失败；DEV 51 CTest 组、clean 449 原生、双方 81 GUI 通过；每分支 640 个 2/20 秒场景、7,040,000 tick，20 份 CSV 一致。开发种子 2026101801/179424673、独立验证 86028121/67867967。
- 明确敌方证据且无手动的 128 例均改善（8.524→1.772 px）；其余组有变差，轨迹解释为稳定源点让 ADS 正常完成并受既有 BodyLock 证据/范围规则降权，没有调整权限掩盖。仿真假设、全部差异见 `docs/project/GEOMETRY_CONTINUITY_20261018.md`。
- clean 实际 Release/React 构建、最终 exe 提交与 v4 核验、合并后的关闭输出真实 GUI 初始化/Applied 热更/IPC/停止通过。首次最终链接受旧实机进程占用失败；用户授权正常停止，执行前重查已退出，重编译通过。个人配置/模型/日志保持，等待实机反馈。
- SyncSet `sync-20261018-002`：accept_draft，沿用用户的修复/验证上下文记录授权；新增[几何决策](decisions/DEC-2026-10-18-001-continuous-source-chest-geometry.md)，更新两端交接和旧决策的几何条款说明。无原始日志/视频或秘密写入，无实机验收推断；历史日志保留，建议单独压缩归档。

## 2026-10-18 — 最低追赶参考与 ADS 刹车组合修复

- 用户明确最低速度仍要克服人物框内过度减速，所有阶段最终服务于稳定到点；授权修复、日志/求解器回归、独立种子短长闭环、同步 clean 编译和上下文更新。
- DEV `3a5d5ec`；临时 `codex/ads-braking-20261018` 提交 `e08cb92` 已快进合入 clean。后置最低值将刹车残余重新放大并转移正交轴需求；修复先建立未刹车追赶增益，再组合运动项，后续仅施加上界。保留最低参考、远距离饱和追赶、胸部、手动权、BodyLock 和 32/40 ms，无新延迟/开关。
- 原版两项核心回归失败、修复后通过；50 对新日志观测构成 92 生产 solver 反例。DEV 51 CTest 组、clean 453 原生、双方 81 GUI 通过。开发种子 2026101819/32452867，最终独立 104395303/67867979；每端 384 个 2/20 秒场景、4,224,000 tick，8 份 CSV 相同。不变量通过不代表所有行为改善。
- 静止组后段 4 px 内比例 54.1%→70.1%、均值误差 10.711→8.217 px；首次到 20 px 晚约 2.3 ms、到 2 px 晚约 18.3 ms。移动组 12.913→13.596 px 退化，未加强弱证据 BodyLock 权限；高响应/长延迟个别例仍不稳定。记录几何缺内部速度/响应，非完整实机重演，详情见 `docs/project/ADS_BRAKING_20261018.md`。
- 实际两端 Release、clean React 构建成功，exe 内嵌提交核对通过；合并后真实 clean GUI 初始化/模型加载/Applied 热更/IPC/正常停止通过（独立配置，关闭输出/震动）。个人配置/模型/日志保持，等待实机慢开镜和移动目标复测。
- SyncSet `sync-20261018-003`：accept_draft，明确写入授权；更新两端交接、[新决策](decisions/DEC-2026-10-18-002-pursuit-reference-before-braking.md) 与旧最低参考条款。无实机验收推断、秘密或原始视频复制；历史日志保留，归档另行建议。

## 2026-10-18 — 整体停稳职责修复、验证并同步 clean

- 用户明确按整体停稳目标修复，不针对单个片段堆补丁，保留最低追赶/胸部/手动/32–40 ms，不加权限、控制等待或开关。
- DEV 实现 `f443913`、说明/轨迹 `2793694`；临时 clean `codex/settling-chain-20261018` 的 `86b34ea` 快进合入。分离真实接近速度与开火扰动分类，位置预算计入已交付工作和整形余量；物理相机跨阶段连续，响应/时差同区间辨识、目标运动单位连续。细节见 [决策](decisions/DEC-2026-10-18-003-whole-chain-settling-ownership.md) 和 `docs/project/SETTLING_CHAIN_20261018.md`。
- 93 个日志 capture 的生产回归旧版失败、新版通过，前轮 92 个 solver 反例通过；DEV 51 CTest、clean 458 原生、两端 81 GUI/WebView2 通过。GUI 测试异步保存竞态在测试侧修正，产品 UI 未改。
- 开发种子 2026101829/49979693、独立 86028157/104395337；每端 384 例、4,224,000 tick，8 份 CSV 相同。静止均值 7.404→4.264 px、后段 4 px 内时间 69.1%→94.7%；移动 15.337→11.261、开火/遮挡 20.718→10.977；独立种子方向同样改善。
- 静止/移动首次进 20 px 晚约 10/11.5 ms；静止 8/64 首周期未进 2 px，后段约 2.24–3.30 px。逐 tick 证明 solver 有微小请求，但既有 3% AI 输入过滤清零；不改 Profile、不补最低输出、不伪造 ADS 完成。手动组贴点比例下降，有限权限和大机动仍有残差，不能说全例改善。
- 实际两端 Release、clean React 构建成功，exe 对应各自 HEAD；18 份生产源文本一致。合并后实际 clean 的独立配置真实模型初始化/Applied 热更/IPC/正常停止通过，关闭设备输出与震动。模型/个人配置/日志未覆盖，仿真不代表实机验收。
- SyncSet `sync-20261018-004`：accept_draft，用户明确授权更新；两端交接、新决策及前轮决策补充已写入。根因和行为解释标记工程证据，不伪称用户实机认可；无秘密、原始视频或外部写入。长历史保持，建议后续单独归档。


## 2026-10-18 — 3% 观测降噪候选，暂不发布

- 用户授权修复3%过滤，保留降噪/最低追赶/胸部/手动/32–40 ms，开发与独立短长闭环通过才合clean。
- 生产DEV `f9f42d3`，clean候选`1cb9a12`（`codex/evidence-noise-20261018`）已编译；实际clean仍`86b34ea`。最终输出死区移至capture位置证据，相机对齐、当前时刻拟合、不改变源点或配置数值；BodyLock速度累计保持。详见新决策004与报告。
- DEV51CTest；候选446Base+14开火；各656场景/7,216,000 tick，12份CSV一致。开发2026101841/32452897，独立49979711/104395367。小偏差/慢移明显改善，测量噪声RMS0.286→0.262 px；纯噪声最终输出比旧硬死区活跃，未宣称全例改善。
- 前轮高速弱证据后段15.721→21.976 px，明确敌人1.557→1.505。反例旧ADS卡住而保留更高权限，新版到点正常交接后7.36%BodyLock不足以维持峰值0.0942需求。故未满足整体行为发布条件，不提高权限、不延迟交接，实际clean未合入。等待用户明确接受边界或给出下一步取舍。
- 候选GUI首次预览JSON截断未稳定复现，保存原始失败；后续81项正常。WebView2测试另发现异步保存时目录扫描暂空而索引失败，修正测试既有轮询，最终通过；产品UI未增加重试或延时。
- SyncSet `sync-20261018-005`：accept_draft，用户明确授权记录；更新两端handoff、追加历史、新决策proposed。用户目标与工程推断分开，未把候选当成已发布/实机认可；无敏感数据、原始视频或外部消息。历史保留，归档另行建议。

- 收尾：DEV文档/诊断`a11f221`，生产仍`f9f42d3`；DEV/候选/实际clean三份exe内嵌版本均匹配各自HEAD，8份候选生产文件文本相同。候选真实模型初始化/Applied热更/IPC/正常停止通过（独立配置、输出与震动关闭）。实际clean没有合入；发布取舍待用户确认。


## 2026-10-18 — BodyLock权重/预算分责分析，纠正主要归因为预算不足

- 用户明确不接受弱证据移动退步、暂不合clean；要求保留手动/32–40ms，不加权限、不延迟交接，先定位根因与可行方案。本轮生产文件和版本不变。
- 7.36%=0.92可靠度×0.08未确认敌人近区系数；solver把它同时用作位置/总运动的增益和最终物理预算。不是a²；BodyLock入口min不重复乘，无手动仲裁也不二次衰减。原case8的1000个BodyLock tick仅21个限幅，平均使用预算49.25%，前轮解释过早。
- 生产反例：零误差、响应1000、恒速60px/s，需求0.06<预算0.0736，却输出0.004416。外置180点上组合、24个理想短长闭环共264000tick；只取消运动项缩放、原预算保持的反事实将20秒后段误差70.192降至0.0024px，峰值0.06174。该实验不含完整估计/噪声/手动/开火/交接，不能当作产品修复。
- 同一身份/证据的69.9与70.1px在生产coordinator产生0.0736与0.2208，需独立审查距离/权限职责；未修改规则。峰值超预算仍真实存在，但不是预算内低输出的理由。
- 推荐分开证据与运动估计、总运动需求、最终预算与手动仲裁；先查有效维持运动的重复缩放，再单独验证位置增益语义。不得照抄夹具反事实中的除法补偿，不提高上限/加最低值/延迟ADS。详见`docs/project/BODYLOCK_AUTHORITY_20261018.md`与`tests/bodylock_authority_20261018/README.md`。
- SyncSet `sync-20261018-006`：accept_draft；沿用用户上下文写入授权，记录明确拒绝发布和工程证据，纠正前轮结论；方案仍待验证。无生产/clean合并、敏感数据或外部写入。历史未删，归档另议。


## 2026-10-18 — 有效维持速度单次消费，dev候选及未通过行为

- 用户授权dev修复有效总目标速度重复缩小，预算、最低追赶、胸部、手动和32/40ms保持；位置增益与70px仅分析，明确不合clean。生产/验证提交15a292d，实际clean仍86b34ea，旧3%候选仍1cb9a12。
- 先红后绿的生产反例：R1000、60px/s、误差0、预算.0736，旧.004416→新.06。solver只把canonical有效总速度从身份增益中分离，保留位置增益和曲线后最终二维预算；无除法补偿/权限/时效/配置改变。
- dev Release、51 CTest（447 Base）、93capture与92solver反例通过。开发2026101853/32452909、独立49979731/104395409；每版本928例2/20秒、10,208,000tick。正式.0736夹具v3明确cue已检查但缺失；v1/v2的ADS/权限适用性问题已记录并排除。另两颗探索种子不计最终统计。
- 独立预算内到点二维恒速30.683→.226px，20秒38.142→.105；原高速弱证据23.848→6.623。原预算/源点/手动/时效约束和阶段互斥断言通过。
- **整体停稳未通过，不能接受退步**：.0736噪声RMS .0421→.2941px、峰值.897；明确敌人开火8.246→8.603；随机慢移2.281→3.075。测量降噪仍.2847→.2622，但运动入口原始位移率仍带噪；不能将旧宽松上界的PASS当作无退步。
- 独立生产探针证明非线性压枪/跟踪合成坐标不一致：残余相机响应逆曲线再加压枪杆量不等于合成总响应后逆曲线；预算.3325/.92零误差反例过补.138603，.0736则欠.028211响应。只定位未修改。位置增益局部时间344/115/27.5ms与70px三倍权限跳变也仅记录未改。
- 新报告docs/project/SUSTAINING_MOTION_20261018.md，复跑tests/sustaining_motion_20261018；原始/逐例比较runs/sustaining-motion-20261018。仿真浮点FIFO、实际量化/检测/枪械/latest-only调度有保真缺口，无实机验收。
- SyncSet sync-20261018-007：accept_draft，用户明确授权更新；handoff、本文、新决策005。用户确认范围与工程证据、候选未通过分开记录，无秘密、外部写入、clean变更。旧历史保留，建议后续单独归档。
