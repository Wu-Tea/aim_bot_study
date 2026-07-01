# ADR-002：原生 WASAPI process loopback 为主捕获路径

状态：Accepted

## 决定

使用 `ActivateAudioInterfaceAsync` 和 process loopback 捕获指定 PID 及子进程音频。传统 endpoint loopback 仅作为回退。

## 原因

- 可隔离目标进程，降低系统通知、语音和其他应用干扰；
- Microsoft 提供 C++ 官方样例；
- miniaudio 的普通 loopback 不能完全代替 process-specific API。

## 后果

- 需要 COM 异步激活和严格生命周期；
- 无活跃 render stream 时会收到静音；
- 必须提供 WAV replay 使算法测试不依赖设备。
