from controller import ControllerFactory
from vision import process_vision

def main():
    # 1. 拿到手柄实例 (此时 controller 开始在后台跑 pygame 事件监听)
    # controller = ControllerFactory.get_controller(use_gamepad=True)

    # 如果你想测试我们刚写的键鼠转手柄新架构：
    # controller = ControllerFactory.get_controller(controller_mode="kbm_to_gamepad")

    # 如果你想用回之前调试好的原生手柄：
    controller = ControllerFactory.get_controller(controller_mode="gamepad")

    # 如果想用纯鼠标跑 YOLO：
    # controller = ControllerFactory.get_controller(controller_mode="mouse")
    print("[Ready] 核心引擎已启动，正在移交视觉控制权...")

    try:
        # 2. 【关键】把 controller 传进去！
        # 这一步如果不传，vision 里的代码就找不到 controller 对象，瞄准就会失效
        process_vision(controller=controller)

    except KeyboardInterrupt:
        pass
    finally:
        if controller: controller.reset()
        print("[Exit] 程序退出")

if __name__ == "__main__":
    main()