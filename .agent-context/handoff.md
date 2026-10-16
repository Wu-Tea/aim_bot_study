# clean 当前接续

当前分支从 DEV `aa0f918` 重建，保留完整 React/WebView2 桌面助手、原生手柄 runtime、固定压枪与实际使用的依赖。GUI 与 Fusion 是两个不同组件，入口和结构见 [README](../README.md)。

旧 clean 原样归档在分支 `codex/runtime-clean-archive-20261016`（`ca0ea11`），独立归档工作目录保留原源码及本地资产。删除前的验证材料还可从既有归档分支追溯。本分支不携带测试、实验记录或历史文档。

本地模型、设备 DLL 和桌面配置库已准备；个人配置与模型保持本地。验证使用 DEV 夹具和记录目录，产品构建输出位于 `native/build/Release/` 与 `python/desktop_app/web_ui/dist/`。

下一动作：使用完成验证的产品版本；新的大改动另开临时分支。实际游戏手感和物理设备交付仍需现场确认，不能以仿真或关闭输出的运行代替。
