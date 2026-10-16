# 本地模型

桌面助手读取本机 TensorRT `.engine`，通过原生 `--inspect-engine` 检查输入尺寸。示例配置使用 `best_480x384.engine`；实际模型在 GUI 中选择。

模型文件不进入 Git。需要转换已有 ONNX 时可使用 `tools/build_engine.ps1` 与本机 TensorRT SDK；训练工具和模型实验记录留在 DEV。不要按文件年龄删除个人模型。
