# 交付 Backlog

任务按顺序执行。每个里程碑必须满足 `docs/06_testing_acceptance.md` 后才进入下一阶段。

## M0 — 仓库与工程基线

- M0-01：C++20/CMake target 和 presets；
- M0-02：vcpkg manifest/baseline 或等价固定依赖；
- M0-03：CTest + Catch2；
- M0-04：format/lint/warnings；
- M0-05：Windows CI Debug/Release；
- M0-06：版本、build info、第三方通知；
- M0-07：生成测试资产脚本；
- M0-08：错误/Result 类型和日志策略。

## M1 — 音频输入与可复现管线

- M1-01：领域音频类型；
- M1-02：预分配 SPSC ring buffer；
- M1-03：synthetic source；
- M1-04：WAV replay source；
- M1-05：PCM format normalize；
- M1-06：process-loopback source；
- M1-07：endpoint-loopback fallback；
- M1-08：start/stop/process-exit/device-error 状态机；
- M1-09：audio smoke CLI；
- M1-10：实时 callback instrumentation。

## M2 — 方向基线

- M2-01：frame/window/STFT；
- M2-02：RMS/energy gate；
- M2-03：wideband + bandwise ILD；
- M2-04：GCC-PHAT；
- M2-05：confidence fusion；
- M2-06：left/center/right/unknown；
- M2-07：hysteresis/smoothing；
- M2-08：offline metrics JSON。

Go/No-Go：若 synthetic 正确但真实目标录音无稳定左右信息，暂停 UI/ML，先完成可行性数据报告。

## M3 — 目标事件检测 baseline

- M3-01：模板资产格式；
- M3-02：log-mel 特征；
- M3-03：模板相似度；
- M3-04：negative gate；
- M3-05：事件合并/cooldown；
- M3-06：detector + direction gating；
- M3-07：离线评估 CLI；
- M3-08：错误分析报告。

## M4 — 外部显示层

- M4-01：Win32 layered window；
- M4-02：Direct2D/DirectWrite renderer；
- M4-03：presentation snapshot；
- M4-04：direction HUD；
- M4-05：settings/diagnostics；
- M4-06：hotkeys、tray、lock；
- M4-07：DPI/多显示器；
- M4-08：UI accessibility checklist。

## M5 — ONNX 模型

- M5-01：模型 metadata contract；
- M5-02：optional ONNX CMake integration；
- M5-03：feature parity tests (Python vs C++)；
- M5-04：CPU inference worker；
- M5-05：multi-head event/direction；
- M5-06：calibration/unknown threshold；
- M5-07：dataset evaluator；
- M5-08：performance/accuracy report。

Go/No-Go：4/8 方位未达到 PRD 门槛时，不默认开启；保留更粗方向。

## M6 — 发布与可选扩展

- M6-01：installer/uninstaller；
- M6-02：clean machine test；
- M6-03：SBOM/third-party notices；
- M6-04：privacy/security review；
- M6-05：crash handling/log rotation；
- M6-06：signed release（若有证书）；
- M6-07：OBS plugin spike；
- M6-08：发布说明和已知限制。

## 暂不排期的研究项

- 多声源分离/多目标定位；
- 高度和距离；
- 自适应 HRTF；
- 跨游戏泛化；
- 5.1/7.1 原生多声道模式；
- 第一方游戏 SDK/event-emitter 模式。
