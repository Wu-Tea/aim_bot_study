# 外部显示层与 UI 规范

## 1. 设计目标

- 只显示足够可靠的信息；
- 低干扰、可缩放；
- 不依赖单一颜色；
- 独立于目标进程；
- 任何状态都可解释和关闭。

## 2. 技术边界

- 独立 Win32 顶层窗口；
- layered/alpha-blended window + Direct2D/DirectWrite；
- 不注入目标进程，不 hook swap chain；
- 不实现防截图、防录屏、窗口隐藏或规避检测；
- 不强制 always-on-top，用户可选择；
- 锁定模式可 click-through；设置模式恢复输入。

## 3. 显示元素

### 最小 HUD

- 中心附近的弧形/边缘箭头；
- 方向：左、中、右或 unknown；
- 事件符号/短标签；
- 置信度可用透明度和线宽表达，但仍需可选数字；
- 事件保持时间和淡出。

### 诊断面板

- source/PID；
- capture mode；
- format；
- RMS L/R、ILD、GCC lag；
- detector/direction confidence；
- queue depth、overflow、latency；
- model/config hash；
- degraded/error reason。

诊断面板默认关闭，适合开发和校准。

## 4. 交互

- `Ctrl+Shift+F10`：显示/隐藏；
- `Ctrl+Shift+F11`：锁定/解锁；
- 热键必须可配置并处理冲突；
- Esc 退出设置模式，不强行退出应用；
- 托盘菜单：选择 source、暂停、设置、诊断、退出。

实际热键可调整，但必须集中配置、可禁用。

## 5. 状态表现

| 状态 | 表现 |
|---|---|
| Idle | 不显示方向；托盘正常 |
| Starting | 小型 spinner/“正在连接音频” |
| Capturing/no event | HUD 可完全隐藏 |
| Detected/known | 显示方向 + 事件 |
| Unknown | 可显示中性环或完全 suppress，由用户设置 |
| Degraded | 非侵入提示，例如“单声道：仅检测” |
| Error | 设置面板给出可操作错误，不在屏幕持续闪烁 |

## 6. 无障碍

- 不以红/绿区别唯一状态；
- 左/右使用形状、位置和文字；
- 支持 100%-300% 缩放；
- 支持高对比轮廓；
- 支持减少动画；
- 所有设置可键盘操作；
- 调试 UI 可用 Dear ImGui，但最终设置窗口应评估标准 Win32/WinUI 控件的可访问性。

## 7. 动画和平滑

- 方向出现：50-100 ms；
- 稳定保持：由事件持续和最短保持时间决定；
- 淡出：100-300 ms；
- 方向改变需要 hysteresis，避免左右跳变；
- UI 不得把过期事件持续显示超过 configured TTL。

## 8. OBS 扩展

OBS 是后续可选输出：

- 独立 source/filter 消费本地事件流或库接口；
- 不与 overlay 共用游戏注入技术；
- 插件 ABI/OBS 版本兼容单独管理；
- OBS 功能不得成为核心捕获/算法测试的依赖。
