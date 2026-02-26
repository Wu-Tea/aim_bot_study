import XInput
import time

print("正在检测物理手柄...")
if not XInput.get_connected()[0]:
    print("❌ 致命错误：系统没有检测到任何 XInput 手柄！")
else:
    print("✅ 成功检测到手柄！请随便推推摇杆、按按扳机 (按 Ctrl+C 退出)...")
    try:
        while True:
            state = XInput.get_state(0)
            if state:
                # 实时打印左摇杆、左扳机和按键掩码
                lx = state.Gamepad.sThumbLX
                l2 = state.Gamepad.bLeftTrigger
                buttons = state.Gamepad.wButtons
                print(f"左摇杆: {lx:6d} | L2扳机: {l2:3d} | 按键掩码: {buttons}")
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass