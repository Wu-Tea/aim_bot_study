# ADR-003：独立外部 overlay，禁止注入

状态：Accepted

## 决定

显示层为独立 Win32 顶层窗口，使用 layered window 和 Direct2D/DirectWrite。不得注入或 hook 目标进程。

## 原因

- 降低稳定性、安全和公平性风险；
- 使显示与捕获/算法解耦；
- 可在非游戏 WAV replay 模式验证。

## 后果

- 某些独占全屏环境可能无法显示；产品应说明限制，不采用注入绕过；
- 多显示器/DPI/窗口定位需独立处理；
- OBS 输出作为单独扩展。
