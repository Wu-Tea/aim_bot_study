# ADR-001：Windows 11 x64 + C++20 作为 MVP

状态：Accepted

## 决定

MVP 只支持 Windows 11 x64，使用 C++20、MSVC 和 CMake。

## 原因

- 目标 API 是 Windows process application loopback；
- 官方样例要求 build 20348 或以上，Windows 11 覆盖该基线；
- 收窄平台可优先验证音频信息是否足够，而不是分散到跨平台适配；
- C++20 提供 `std::span`、`std::jthread` 等适合本项目的类型。

## 后果

- Linux/macOS 不属于 MVP；
- 核心 DSP 保持可移植，但捕获和 UI 是 Windows adapter；
- 未来跨平台需要新 ADR。
