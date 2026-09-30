import pydirectinput
import time


def custom_afk_bot():
    print("准备开始定制版挂机...")
    print("请在 5 秒内切换到游戏窗口！")
    time.sleep(5)
    print("挂机脚本已启动！将鼠标甩到屏幕四个角落，或在终端按 Ctrl+C 停止。")

    try:
        while True:
            # # 1. 鼠标左键按下 (保持 3.0 秒)
            # pydirectinput.mouseDown(button='left')
            # time.sleep(3.0)
            # pydirectinput.mouseUp(button='left')
            #
            # # 2. 间隔 250 毫秒 (0.25 秒)
            # time.sleep(0.25)
            #
            # # 3. 鼠标右键按下 (保持 3.2 秒)
            # pydirectinput.mouseDown(button='right')
            # time.sleep(3.2)
            # pydirectinput.mouseUp(button='right')
            #
            # # 4. 间隔 150 毫秒 (0.15 秒)
            # time.sleep(0.15)
            #
            # # 5. 按下 X 键 (保持 100 毫秒 / 0.1 秒)
            # pydirectinput.keyDown('x')
            # time.sleep(0.1)
            # pydirectinput.keyUp('x')
            #
            # # 6. 间隔 500 毫秒 (0.5 秒)，然后进入下一次循环
            # time.sleep(0.5)


            # 3. 按一下 X 键
            pydirectinput.press('x')

            # 每一轮循环结束稍微停顿一下，避免占用过高电脑性能
            # time.sleep(0.3)

            for _ in range(2):
                time.sleep(0.3)
                pydirectinput.mouseDown(button='left')
                time.sleep(0.1)
                pydirectinput.mouseUp(button='left')
                # pydirectinput.click()

            # 动作切换缓冲，防止游戏吞键
            time.sleep(0.1)

            # 2. 按一下 V 键
            pydirectinput.press('s')

            # 2. 按一下 V 键
            pydirectinput.press('v')

            # 动作切换缓冲
            time.sleep(0.3)

    except KeyboardInterrupt:

        print("\n收到停止指令，脚本准备退出...")

    except pydirectinput.FailSafeException:

        print("\n触发防故障机制（鼠标移至角落），脚本准备退出...")

    finally:

        # 安全措施：确保退出时释放所有按键和鼠标，防止卡键导致电脑无法正常操作

        pydirectinput.mouseUp(button='left')

        pydirectinput.mouseUp(button='right')

        pydirectinput.keyUp('x')

        print("所有按键已安全释放，脚本完全停止。")


if __name__ == "__main__":
    # 防故障机制保持开启，出问题时鼠标往屏幕角落一甩就能停
    pydirectinput.FAILSAFE = True
    custom_afk_bot()