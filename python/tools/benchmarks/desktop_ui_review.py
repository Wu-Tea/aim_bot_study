"""Capture the retained Tk workspace on an isolated desktop with temporary profiles."""
import ctypes
import os
from pathlib import Path
import sys
import time

from desktop_app.test_desktop import run_isolated, require_isolated_gui
from project_paths import PROJECT_ROOT


def capture(window,path):
    import win32gui
    import win32ui
    from PIL import Image
    window.update()
    hwnd=win32gui.GetAncestor(window.winfo_id(),2)
    left,top,right,bottom=win32gui.GetWindowRect(hwnd)
    width,height=right-left,bottom-top
    handle=win32gui.GetWindowDC(hwnd)
    source=win32ui.CreateDCFromHandle(handle)
    target=source.CreateCompatibleDC()
    bitmap=win32ui.CreateBitmap();bitmap.CreateCompatibleBitmap(source,width,height)
    previous=target.SelectObject(bitmap)
    try:
        draw=ctypes.windll.user32.PrintWindow
        draw.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint]
        if not draw(hwnd,target.GetSafeHdc(),2):raise RuntimeError('PrintWindow failed')
        image=Image.frombuffer('RGB',(width,height),bitmap.GetBitmapBits(True),'raw','BGRX',0,1)
        image.save(path)
    finally:
        target.SelectObject(previous)
        win32gui.DeleteObject(bitmap.GetHandle())
        target.DeleteDC();source.DeleteDC();win32gui.ReleaseDC(hwnd,handle)


def main():
    if not os.environ.get('AIM_GUI_TEST_DESKTOP'):
        raise SystemExit(run_isolated([str(Path(__file__).resolve()),*sys.argv[1:]],cwd=PROJECT_ROOT))
    require_isolated_gui()
    sys.path.insert(0,str(PROJECT_ROOT/'python/tests'))
    from test_desktop_workspace import DesktopWorkspaceTests
    destination=Path(sys.argv[1]).resolve();destination.mkdir(parents=True,exist_ok=True)
    DesktopWorkspaceTests.setUpClass()
    case=DesktopWorkspaceTests();case.setUp()
    try:
        app,root=case.app,case.root
        app.show_page('ads')
        deadline=time.monotonic()+10
        while app.ads_policy_loading and time.monotonic()<deadline:
            app.poll();root.update();time.sleep(.01)
        if app.ads_policy is None:raise RuntimeError(app.notice.get())
        for size in ('840x680','1120x840'):
            root.geometry(size+'+20+20')
            for page in ('assist','ads','curve','device','feedback'):
                app.show_page(page);root.update()
                capture(root,destination/f'{size}-{page}.png')
                if page=='ads':
                    app.ads_diagram.mode.set('follow');root.update()
                    capture(root,destination/f'{size}-follow.png')
                    app.ads_diagram.mode.set('acquire')
        app.show_page('ads');app.ads_diagram.mode.set('follow')
        for path,value in (('output_limit_x','.8'),('output_limit_y','.8'),
                           ('response_time_x_ms','80'),('response_time_y_ms','80')):
            app.variables['gamepad.bodylock.'+path].set(value)
        root.geometry('840x680+20+20')
        app.ads_diagram.offset=[32.,0.];app.ads_diagram.redraw();root.update()
        capture(root,destination/'near-center-80ms.png')
        app.ads_diagram.offset=[.5,0.];app.ads_diagram.redraw();root.update()
        capture(root,destination/'point-arrival-stop.png')
        app.variables['gamepad.assist.minimum_position_stick'].set('0')
        app.variables['gamepad.assist.arrival_radius_px'].set('1')
        app.ads_diagram.offset=[1.1,0.];app.ads_diagram.redraw();root.update()
        capture(root,destination/'ai-deadzone-filtered.png')
        print(destination)
    finally:
        case.tearDown();DesktopWorkspaceTests.tearDownClass()


if __name__=='__main__':main()
