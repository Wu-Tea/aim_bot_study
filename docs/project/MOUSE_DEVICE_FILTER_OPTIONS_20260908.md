# Mouse 设备过滤方案核查（2026-09-08）

用户目标：应用取得物理鼠标输入，默认原样转写，仅在 controller 判断需要时修改；游戏不得同时收到原始位移与转写位移。目标并非 Garena 版本，Garena 公告不作为本项目区服依据。本次只核查官方资料，没有安装驱动、更改设备隐藏规则或验证游戏接收。

## 已核实的方案边界

| 方案 | 官方资料支持的能力 | 对当前链路的结论 |
| --- | --- | --- |
| HidHide | 对 HID/XInput 设备提供应用访问过滤，但 FAQ 明确不支持鼠标、键盘、触摸板 | 排除直接使用。可在列表中看见设备，不等于可以拦截该设备的鼠标输入 |
| Interception | 鼠标设备过滤、读取和发送 API；项目 mouse_link 与新 transport 已使用同一套接口 | 可编程接管候选；本机驱动预检不可用，真实排除及目标游戏接收均未完成 |
| reWASD | Mute 可取消物理鼠标原生行为，并支持模拟鼠标输出 | 具备接近目标的成品功能；尚未找到可供当前应用逐 tick 提交相对鼠标 counts 的公开接口证据，不能直接视为 controller 后端 |
| Raw Accel | Windows 10/11 x64 驱动，在原始输入流修改鼠标位移；官方发布驱动签名，按固定公式变换输入，设置更新带 1 秒延迟 | 证明设备层修改位移是正常可实现的功能；现成产品不适合作为 1000 Hz AI 位移提交接口，不提出移除其保护设计 |
| MouseMux V2 Windows SDK | 输入通知、桌面指针操作与虚拟用户 API，移动接口使用绝对坐标 | 未找到排除其他进程 Raw Input 物理位移、并提供相对游戏鼠标替换的证据，不作为当前已匹配候选 |
| Microsoft Moufiltr 示例 | 鼠标过滤回调可删除、变换或插入输入包 | 正规驱动开发基础，不是现成可安装的 HidHide 替代品，也不是游戏兼容证明 |

## reWASD 资料的时间边界

2023-07-23 官方支持回复称没有 API，不能只凭这条旧回复断言 2026 年所有功能。当前 9.3 文档已有 Send Input 功能，但描述的是虚拟键盘输入；文档明确该功能不屏蔽原始输入，可能出现双重输入，且存在时序限制。因此它不能作为“已找到鼠标 controller 实时接入 API”的证据。

## 接收验收与选型结论

HidHide 的手柄应用白名单模型不能直接套到鼠标。本项目需要设备输入包层的正常过滤与转写接口。现有 Interception 是能力匹配的候选，reWASD 是具备物理鼠标 Mute 的成品参照；尚无证据证明任一方案在用户的三角洲版本中可用。

真实验收分开记录：所选实体鼠标有非零输入时，零输出模式的独立接收端必须无位移；反向模式只收到反向量；恢复原样转发时输入输出 counts 一致；最后在目标游戏检查实际视角响应。设备列表隐藏、桌面光标不动、驱动加载和发送 API 成功，都不能替代这些接收结果。

## 官方来源

- [HidHide FAQ：不支持鼠标、键盘、触控板](https://docs.nefarius.at/projects/HidHide/FAQ/)
- [HidHide 定位和访问过滤模型](https://docs.nefarius.at/projects/HidHide/)
- [Interception 上游库接口](https://github.com/oblitum/Interception/blob/master/library/interception.h)
- [reWASD：Mute 原生鼠标](https://help.rewasd.com/faq/unmappedmouse.html)
- [reWASD：模拟鼠标设置](https://www.help.rewasd.com/basic-functions/virtual-input-device-settings.html)
- [reWASD：2023 年 API 答复](https://forum.rewasd.com/forum/rewasd/technical-questions-aa/238468-does-rewasd-support-an-api)
- [reWASD 9.3 Send Input 当前功能和限制](https://help.rewasd.com/mapping-features/send-input.html)
- [Raw Accel 官方仓库与固定公式、设置更新时间说明](https://github.com/RawAccelOfficial/rawaccel)
- [MouseMux V2 Windows SDK](https://www.mousemux.com/pages/sdk-windows/)
- [Microsoft MouFilter_ServiceCallback](https://learn.microsoft.com/en-us/previous-versions/ff542380%28v%3Dvs.85%29)
- [Microsoft Moufiltr 源码](https://github.com/microsoft/Windows-driver-samples/blob/main/input/moufiltr/moufiltr.c)
