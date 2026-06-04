# Metadata

## Topic Slug

`vision-gamepad-optimization-report`

## Recommended Title

从看见目标到可信控制：native vision 和 gamepad 的系统化设计

## Title Options

1. 从看见目标到可信控制：native vision 和 gamepad 的系统化设计
2. 实时视觉辅助与手柄控制协同优化报告
3. 从目标识别到手柄控制：一个 FPS 实时辅助系统的工程复盘
4. native vision 与 gamepad 控制路线报告：目标证据、手感和开火边界
5. 面向实时 FPS 场景的视觉目标分级与手柄辅助优化

## Short Summary

这份报告系统梳理了 `yolo-study-001` 中 native vision 和 gamepad 两条路线：vision 如何根据真实 FPS 场景选择 ROI、native 热路径、目标分级、弱关联、黄色 cue 短续住和宽低框黄点 double check；gamepad 如何根据开镜、贴身、侧跑、玩家输入冲突和自动开火风险做 ADS snap、body lock、输入仲裁、projection、auto-fire gate 和 recoil 优化。

## Abstract

本文面向没有完整项目背景的读者，用场景驱动方式解释一个实时 FPS 视觉辅助项目的工程设计。文章不以 git 提交为主线，而以真实问题为主线：延迟为什么要求 native 热路径，开火遮挡为什么需要短续住，多目标和低分框为什么不能直接信任，宽低框为什么需要黄点 double check，gamepad 为什么要区分 ADS snap 与 body lock，自动开火为什么必须比辅助瞄准更保守。报告重点呈现 vision 和 gamepad 之间的权责边界，以及“不同证据只做对应层级动作”的核心设计。

## Tags

- Real-time Vision
- YOLO
- TensorRT
- Native Vision
- Gamepad Control
- Aim Assist
- Target Selection
- Control Fusion
- Target Authority
- Engineering Report

## Style Notes

- 中文正式报告风格。
- 不以提交记录为叙事主线。
- 以场景、问题、策略、效果、边界组织内容。
- Mermaid 图用于解释数据流和权限流。
- 少用未解释的名词，技术细节服务于设计判断。
- 主体文章保持大白话，但整体语气要专业、克制、通透。
