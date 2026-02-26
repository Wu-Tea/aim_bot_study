"""
测试应用：监听鼠标右键或模拟的 L2 键，一旦检测到按下就根据给定偏移调用控制器进行平滑移动。

本程序依赖于已提供的控制器映射文件 ``e9c9dd8b-333c-4aa4-ab07-5552401daeb1.py``，
通过 `ControllerFactory` 创建合适的控制器实例。如果检测到用户按下鼠标右键，
或者按下某个键位（可模拟为手柄的 L2），程序会持续向控制器发送偏移量，
从而观察鼠标/手柄摇杆的平滑修正效果。

由于运行环境可能缺乏第三方输入监听库，这里直接利用 Windows API
``GetAsyncKeyState`` 来检测键盘和鼠标按键状态。当在非 Windows 平台运行时，
代码仍然可以导入和执行，但按键检测将无法生效。

使用方法：
  python test_listener.py [--gamepad]

  其中 ``--gamepad`` 标志表明测试手柄模式（使用 ``GamepadController``），
  默认情况下测试鼠标模式（使用 ``MouseController``）。

若在 Windows 平台运行，你可以按住鼠标右键测试鼠标模式；如果希望模拟
手柄 L2，也可以将 ``VK_L2`` 常量修改为希望监听的键盘虚拟键码，例如
``0x41`` 对应键盘 A 键。具体虚拟键码参考微软文档。手柄模式下按住该键将
触发摇杆平滑移动。

注意：
* 本示例仅用于演示如何集成按键监听与控制器映射，不涉及 YOLO 检测或目标
  偏移量计算逻辑。
* 若需实际监听 DS4 手柄的 L2 按键，建议使用 ``inputs`` 或 ``pygame`` 等第三方
  库读取手柄事件，并在获取到 L2 按压后调用控制器的 ``update`` 方法。
"""

import argparse
import time
import ctypes
import importlib.util
import os

# 动态导入控制器文件。这是因为原文件名包含破折号，无法直接作为模块导入。
_module_name = "controller_module"
_file_path = os.path.join(os.path.dirname(__file__), "e9c9dd8b-333c-4aa4-ab07-5552401daeb1.py")
_spec = importlib.util.spec_from_file_location(_module_name, _file_path)
_module = importlib.util.module_from_spec(_spec)
import sys
# 如果环境中缺失 vgamepad，则创建一个简易的虚拟替代对象，
# 以便执行原控制器脚本时不会抛出 ModuleNotFoundError。
if 'vgamepad' not in sys.modules:
    class _DummyVGamepadModule:
        class VDS4Gamepad:
            def __init__(self, *args, **kwargs):
                print("[Stub] vgamepad.VDS4Gamepad 初始化 (环境中未安装 vgamepad)")
            def right_joystick_float(self, *args, **kwargs):
                # 打印调试信息，模拟摇杆设定
                print(f"[Stub] right_joystick_float called with args={args}, kwargs={kwargs}")
            def update(self):
                # 模拟更新，不执行任何操作
                pass
    sys.modules['vgamepad'] = _DummyVGamepadModule()

# 执行控制器映射脚本
_spec.loader.exec_module(_module)

# 获取 ControllerFactory 类
ControllerFactory = _module.ControllerFactory


# Windows 虚拟键代码
VK_RBUTTON = 0x02  # 鼠标右键
# 模拟的 L2 键，可以根据需要替换为其他键的虚拟键码
# 例如 0x41 表示 A 键、0x44 表示 D 键等
VK_L2 = 0x41  # 这里暂用键盘 A 键模拟手柄 L2


def is_key_pressed(vk_code: int) -> bool:
    """检查给定虚拟键是否处于按下状态（仅限 Windows）。"""
    try:
        # GetAsyncKeyState 返回短整型，其最高位为按下标志
        state = ctypes.windll.user32.GetAsyncKeyState(vk_code)
        return (state & 0x8000) != 0
    except Exception:
        # 在非 Windows 平台上，ctypes.windll 可能不存在
        return False


def main(use_gamepad: bool) -> None:
    """主循环：监听按键并驱动控制器运动。"""
    # 初始化控制器：根据 use_gamepad 参数选择鼠标或手柄控制器
    controller = ControllerFactory.get_controller(use_gamepad=use_gamepad)

    # 为演示目的，设定一个固定的偏移量。实际应用中应来自目标检测。
    delta_x = 50  # 向右偏移 50 像素
    delta_y = 0   # Y 方向无偏移

    print("\n--- 按住鼠标右键或模拟 L2 键开始平滑移动，松开后停止 ---")
    print("在测试手柄模式时，按住键盘 A 键可模拟 L2 按下；你可以根据需要调整 VK_L2 值。")

    try:
        while True:
            right_pressed = is_key_pressed(VK_RBUTTON)
            l2_pressed = is_key_pressed(VK_L2)

            if right_pressed or l2_pressed:
                # 如果按键按下，持续发送偏移量
                controller.update(delta_x, delta_y)
            else:
                # 如果未按下，则释放控制器（仅手柄需要归位）
                controller.reset()

            # 短暂休眠以降低 CPU 占用
            time.sleep(0.01)
    except KeyboardInterrupt:
        # 捕获 Ctrl+C 优雅退出
        pass
    finally:
        # 退出前确保控制器回中
        controller.reset()
        print("\n退出测试应用。")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="测试控制器映射的按键监听应用")
    parser.add_argument('--gamepad', action='store_true', help='使用虚拟手柄模式')
    args = parser.parse_args()

    main(use_gamepad=args.gamepad)