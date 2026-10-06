# 2026-10-06 工作产物索引

此目录保留今天工作区已有的截图、数值模拟、配置试验和检查日志。中间失败与历史假设也保留，不能把所有文件都解释为最终实现。个人配置、模型、构建目录和原始运行 telemetry 继续遵循仓库忽略规则。

| 目录 / 文件 | 用途与时效 |
| --- | --- |
| `point-curve-tuning-20261006/` | 最终连续收尾与短历史速度估计；`ctest.log`、`gui-tests.log`、`build.log` 为最终检查，`comparison.csv/json` 记录全部对比及六组残余误差回退，`before-after.png/svg` 为图；`tests.log` 是中间调试日志，可能含已修复失败。 |
| `point-boundary-20261006/` | 修改前的硬边界复现基线，保留以便前后对照；不是当前曲线。 |
| `incident-20261006-105443/` | Apex 断续与 COD 帧率事件；`findings.json` 的等待复测字样是初步分析，最终 COD 前后数据以 `cod-before-after.csv` 为准，约 66–69 → 166–167 FPS。 |
| `desktop-ui-review-20261006/` | GUI 初版布局检查。 |
| `desktop-parameter-semantics-20261006/` | 独立输出上限／响应时间及单位整理。 |
| `desktop-ai-deadzone*-20261006/` | AI 死区与逐项参数核验截图。 |
| `desktop-range-response*-20261006/`、`range-response-*` | 距离衰减和最小力度的中间方案；其中硬到点归零行为被最终连续收尾替代，旧截图不能作为当前参数说明。 |
| `parameter-audit-*`、`assist-parameters-*` | 参数边界、GUI／原生一致性与构建检查历史。 |

模拟数据使用固定种子、短长窗口和显式 plant，不能代替真实游戏验收。最终比较脚本 `point-curve-tuning-20261006/compare.py` 读取保留的旧基线与新运行的原生测试产物；绘图需要 matplotlib，仅用于离线报告，不参与生产运行或原生构建。复现原生模拟可运行 `cod_native_base_tests --suite BaseBodyLock --artifacts <目录>`。

工作与决策记录位于 `.agent-context/`，实际参数语义以 `native/controller_native/editable_parameters.json` 及 `docs/project/CONFIGURATION_AND_CHECKS.md` 为准。
