from ultralytics import YOLO


def export_trt():
    print("[Export] 开始重新编译 TensorRT 引擎...")
    # 替换成你实际使用的 pt 模型路径
    model = YOLO("yolov8n.pt")

    # 开始转译
    # half=True 开启 FP16 半精度加速，对 40 系显卡提速巨大
    # workspace=4 给 TensorRT 分配 4GB 显存作为编译工作区（可根据情况调大）
    model.export(
        format="engine",
        half=True,
        workspace=8,
        simplify=True
    )
    print("[Export] 编译完成！可以重新启动你的主程序了。")


if __name__ == "__main__":
    export_trt()