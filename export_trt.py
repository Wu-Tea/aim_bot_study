from ultralytics import YOLO


def export_trt():
    print("[Export] 开始重新编译 TensorRT 引擎...")
    # 替换成你实际使用的 pt 模型路径
    model = YOLO("yolo26s-pose.pt")

    # 开始转译
    # half=True 开启 FP16 半精度加速，对 40 系显卡提速巨大
    # workspace=4 给 TensorRT 分配 4GB 显存作为编译工作区（可根据情况调大）
    model.export(
        format="engine",
        half=True,
        imgsz=640,  # 必须显式指定！让 TRT 专门为 640x640 优化
        batch=1,  # 强制静态 Batch=1，不再支持多张图同时输入，换取单图极限速度
        workspace=8,  # 编译期的搜索空间
        simplify=True,
        device=0
    )
    print("[Export] 编译完成！可以重新启动你的主程序了。")


if __name__ == "__main__":
    export_trt()