# clean 缺帧身份与预测的 32/40 ms 边界

- Status: accepted（已提交的产品契约；不代表实机效果验收）
- Date: 2026-10-16
- Confirmed by: 用户授权保留手动权/时效的修复并在验证后合回，随后明确要求记录边界；实现与说明已提交于 clean `739bc3d`。
- Related sessions: 2026-10-16 clean 日志/视频分析与修复；日志会话 `20261016T123101Z_39880_1`。
- Related files: clean `native/controller_native/target_coordinator.cpp`、`native/pipeline_contract/target_acquisition.h`、`docs/ARCHITECTURE.md`；DEV `tests/clean_incident_20261016/`。
- Supersedes: 无整条决策替代；澄清 [2026-10-06 延续语义](DEC-2026-10-06-001-assist-parameter-and-continuity-semantics.md) 中 32 ms 约束的是缺帧预测，不是可额外放宽的输出寿命。
- Superseded by: 仅第 1 项的姿态类别切换瞄点行为由 [2026-10-18 连续几何](DEC-2026-10-18-001-continuous-source-chest-geometry.md) 替代；准入、身份与时限条款仍有效。

## 背景与决定

末段视频对应日志中，同一 selector generation 的目标在原 capture 年龄越过 32 ms 时被清除，未完成 ADS 随之结束；几毫秒后新鲜观测回来只能走 BodyLock。另有五组身体框因高宽比处在 0.65–0.85 而被几何准入拒绝。

1. 身体框使用连续准入范围，姿态分类只影响几何瞄点；类别、友军、置信度、尺寸与面积检查保留。
2. 缺帧预测仅在原 capture 年龄不超过 32 ms 且证据/区域约束有效时允许。超过 32 ms 后不再授予预测或辅助瞄准权，缺帧不授予开火权。
3. 同 generation 的身份与未完成 ADS 可保留到既有硬来源时限；live runtime 为 40 ms，达到该值释放。32–40 ms 区间仅存身份，不能因身份存在继续输出。
4. 重复缺帧不刷新 capture 时间。友军/可靠性拒绝、身份切换、作用域退出、手动换目标/显式退出继续撤销延续。逐轴手动权、原始透传、源选定瞄点、曲线与输出上限不变。

## 理由与未采用方案

身份连续性与预测证据有不同职责；在仍有效的来源硬时限内保留身份，可恢复原任务而无需制造新 ADS 令牌。未采用延长预测/40 ms 硬时限、在缺帧时续期、减弱人工反向权、叠加延迟开关或扩大尺寸/面积准入来掩盖缺陷。

## 证据与后果

clean `739bc3d`，DEV 夹具 `07ae3e8`；[结果](../../runs/clean-incident-fix-20261016/RESULTS.md)记录 3 组新回归、448 原生、80 GUI、128 随机场景/1,408,000 tick 及关闭输出的真实 GUI/runtime 检查。预测时限、身份硬时限和明确撤销分别检查；旧测试中合并二者的假设仅在生成副本校正，原因可查。

32/40 ms 是当前 live 配置的解释：通用 TargetCoordinator 的硬时限来自配置，不能把其默认 50 ms 或测试用值误认为 live 已放宽。2026-10-18 已在 DEV `b0269f0` 同步生产修复；原生夹具直接检查两种时限。DEV/clean 的同种子场景与既有回放比较通过，见 [同步报告](../../docs/project/DEV_CLEAN_SYNC_20261018.md)。

## 复查条件

用户下一轮实机若仍出现跟不上、定位迟滞、人工反向冲突或遮挡后跳转，以新日志/视频区分模型完全漏检、几何准入、来源超时和状态问题。仿真使用显式合成相机，不能代替实机验收；当前实机结果待反馈。
