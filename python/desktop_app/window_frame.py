"""Keep Windows sizing/system commands while drawing the caption in the WebView."""
import ctypes
from ctypes import wintypes as w


def install(window):
    """Install on the WinForms UI thread, before the window is shown."""
    from time import perf_counter
    started = perf_counter()
    native = window.native
    native.ShowIcon = False
    original_bounds = native.Bounds
    from System.Windows.Forms import FormBorderStyle
    native.FormBorderStyle = getattr(FormBorderStyle, 'None')
    hwnd = native.Handle.ToInt64()
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.GetWindowLongW.argtypes = [w.HWND, ctypes.c_int]
    user.SetWindowLongW.argtypes = [w.HWND, ctypes.c_int, ctypes.c_long]
    user.SetWindowLongW(hwnd, -16, user.GetWindowLongW(hwnd, -16) | 0x000F0000)
    # Keep high-frequency window messages in .NET instead of calling Python
    # for every sizing, painting and hit-test message.
    import clr
    clr.AddReference('Microsoft.CSharp')
    from Microsoft.CSharp import CSharpCodeProvider
    from System.CodeDom.Compiler import CompilerParameters
    from System import Array, String
    compiler = CSharpCodeProvider()
    options = CompilerParameters()
    options.GenerateInMemory = True
    options.ReferencedAssemblies.Add('System.dll')
    options.ReferencedAssemblies.Add('System.Windows.Forms.dll')
    options.ReferencedAssemblies.Add('System.Drawing.dll')
    source = r"""
using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;
public sealed class IntegratedCaption : NativeWindow {
    [StructLayout(LayoutKind.Sequential)] struct PointI { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] struct MinMax {
        public PointI reserved, maxSize, maxPosition, minTrack, maxTrack;
    }
    readonly Form form;
    int Border { get { return Math.Max(4, (int)Math.Round(6 * form.DeviceDpi / 96.0)); } }
    public IntegratedCaption(Form owner) {
        form = owner;
        AssignHandle(owner.Handle);
        owner.Resize += delegate { UpdateInset(); };
        UpdateInset();
    }
    void UpdateInset() {
        form.Padding = new Padding(form.WindowState == FormWindowState.Maximized ? 0 : Border);
    }
    protected override void WndProc(ref Message m) {
        // FormBorderStyle.None and a full client rectangle agree on restore geometry.
        // WS_THICKFRAME keeps Windows sizing/snap; padding exposes its resize edges.
        if (m.Msg == 0x83 && m.WParam != IntPtr.Zero) { m.Result = IntPtr.Zero; return; }
        base.WndProc(ref m);
        if (m.Msg == 0x24) {
            MinMax bounds = (MinMax)Marshal.PtrToStructure(m.LParam, typeof(MinMax));
            Screen screen = Screen.FromHandle(Handle);
            bounds.maxPosition.x = screen.WorkingArea.Left - screen.Bounds.Left;
            bounds.maxPosition.y = screen.WorkingArea.Top - screen.Bounds.Top;
            bounds.maxSize.x = screen.WorkingArea.Width;
            bounds.maxSize.y = screen.WorkingArea.Height;
            Marshal.StructureToPtr(bounds, m.LParam, false);
        }
        if (m.Msg == 0x84) {
            long pos = m.LParam.ToInt64();
            Point p = form.PointToClient(new Point((short)(pos & 65535), (short)((pos >> 16) & 65535)));
            int hit = 1;
            if (form.WindowState == FormWindowState.Normal) {
                bool left = p.X < Border, right = p.X >= form.ClientSize.Width - Border;
                bool top = p.Y < Border, bottom = p.Y >= form.ClientSize.Height - Border;
                hit = top ? (left ? 13 : right ? 14 : 12) :
                    bottom ? (left ? 16 : right ? 17 : 15) : left ? 10 : right ? 11 : 1;
            }
            m.Result = new IntPtr(hit);
        }
    }
}
"""
    compiled = compiler.CompileAssemblyFromSource(options, Array[String]([source]))
    if compiled.Errors.HasErrors:
        raise RuntimeError('Window frame compilation failed: ' + str(compiled.Errors[0]))
    from System import Activator, Object
    window._integrated_caption = Activator.CreateInstance(
        compiled.CompiledAssembly.GetType('IntegratedCaption'), Array[Object]([native]))
    compiler.Dispose()
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int,
                                 ctypes.c_int, ctypes.c_int, w.UINT]
    native.Bounds = original_bounds
    user.SetWindowPos(hwnd, None, 0, 0, 0, 0, 0x27)

    def enable_regions(*_):
        native.webview.CoreWebView2.Settings.IsNonClientRegionSupportEnabled = True

    if native.webview.CoreWebView2:
        enable_regions()
    else:
        native.webview.CoreWebView2InitializationCompleted += enable_regions
    window._frame_setup_ms = (perf_counter() - started) * 1000


def command(window, name):
    """Only explicit window-button actions cross the bridge; no polling."""
    from System import Action
    from System.Windows.Forms import FormWindowState
    result = {}
    def apply():
        native = window.native
        if name == 'window_minimize':
            native.WindowState = FormWindowState.Minimized
        elif name == 'window_toggle_maximize':
            native.WindowState = (FormWindowState.Normal
                if native.WindowState == FormWindowState.Maximized else FormWindowState.Maximized)
        result['maximized'] = native.WindowState == FormWindowState.Maximized
    window.native.Invoke(Action(apply))
    return {'ok': True, 'data': result}
