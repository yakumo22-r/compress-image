"""Windows GUI regression. Run from the repo root with a non-embedded ykm build."""
import argparse
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess
import tempfile
import time


def check(value, message):
    if not value:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', default='build/windows/x64/debug/ykm.exe')
    args = parser.parse_args()
    exe = str(Path(args.exe).resolve())
    u = c.WinDLL('user32', use_last_error=True)
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    u.EnumWindows.argtypes = [callback_type, w.LPARAM]
    u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    u.IsWindowVisible.argtypes = [w.HWND]
    u.IsHungAppWindow.argtypes = [w.HWND]
    u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
    u.SetForegroundWindow.argtypes = [w.HWND]
    u.GetForegroundWindow.restype = w.HWND

    class GuiInfo(c.Structure):
        _fields_ = [('size', w.DWORD), ('flags', w.DWORD),
                    ('active', w.HWND), ('focus', w.HWND), ('capture', w.HWND),
                    ('menu', w.HWND), ('move', w.HWND), ('caret', w.HWND), ('rect', w.RECT)]

    u.GetGUIThreadInfo.argtypes = [w.DWORD, c.POINTER(GuiInfo)]

    with tempfile.TemporaryDirectory(prefix='ykm-win32-') as temporary:
        root = Path(temporary)
        widget = root / 'probe'
        widget.mkdir()
        (widget / 'main.js').write_text("""
ykm.logging.enable('frames.log');
const root = new ykm.ui.Window({key:'one',title:'Regression One',width:480,height:320});
let frames = 0;
function render() { ykm.print('FRAME '+(++frames)); return root; }
""", encoding='utf-8')
        second_widget = root / 'second'
        second_widget.mkdir()
        (second_widget / 'main.js').write_text("const root = new ykm.ui.Window({key:'two',title:'Regression Two',width:480,height:320}); function render(){return root;}", encoding='utf-8');
        with (root / 'stdout.log').open('wb') as output:
            process = subprocess.Popen([exe, str(root)], cwd=root, stdout=output, stderr=subprocess.STDOUT)
            try:
                def windows():
                    result = []

                    @callback_type
                    def visit(hwnd, _):
                        pid = w.DWORD()
                        u.GetWindowThreadProcessId(hwnd, c.byref(pid))
                        if pid.value == process.pid and u.IsWindowVisible(hwnd):
                            result.append(hwnd)
                        return True

                    u.EnumWindows(visit, 0)
                    return result

                def frames():
                    log = root / 'frames.log'
                    return log.read_text(encoding='utf-8', errors='replace').count('FRAME') if log.exists() else 0

                def gui_state(hwnd):
                    info = GuiInfo()
                    info.size = c.sizeof(info)
                    thread = u.GetWindowThreadProcessId(hwnd, None)
                    check(thread != 0 and u.GetGUIThreadInfo(thread, c.byref(info)),
                          'GetGUIThreadInfo failed')
                    return info

                def modal(hwnd):
                    info = gui_state(hwnd)
                    return bool(info.flags & 2) and info.move == hwnd  # GUI_INMOVESIZE

                def diagnostics(hwnd):
                    info = gui_state(hwnd)
                    return (f'pid={process.pid}, exit={process.poll()}, target={hwnd}, '
                            f'flags={info.flags:#x}, move={info.move}, active={info.active}, '
                            f'capture={info.capture}, foreground={u.GetForegroundWindow()}, '
                            f'frames={frames()}')

                deadline = time.monotonic() + 15
                while time.monotonic() < deadline and process.poll() is None:
                    if len(windows()) == 2 and frames() >= 5:
                        break
                    time.sleep(.1)
                check(len(windows()) == 2 and frames() >= 5,
                      'Expected two test windows and frame log; build with --ykm_app=""')
                first, second = windows()
                for i in range(12):
                    check(u.SetWindowPos(first, None, 0, 0, 480 + i * 9, 340 + i * 5, 0x16),
                          'SetWindowPos failed')
                    time.sleep(.08)
                check(process.poll() is None and not u.IsHungAppWindow(first), 'Resize crash/hang')
                u.SetForegroundWindow(first)
                time.sleep(.2)
                check(u.PostMessageW(first, 0x112, 0xf008, 0), 'SC_SIZE post failed')
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline and process.poll() is None and not modal(first):
                    time.sleep(.02)
                check(process.poll() is None and modal(first),
                      'Did not enter native size loop: ' + diagnostics(first))
                before = frames()
                time.sleep(2)
                delta = frames() - before
                check(modal(first), f'Size loop ended during measurement; delta={delta}; ' + diagnostics(first))
                check(delta >= 10, f'Render stalled during size loop: {delta} frames')
                print(f'Native modal render: {delta} frames / 2 seconds')
                u.PostMessageW(first, 0x100, 0x1b, 0)  # Escape
                time.sleep(.5)
                check(not modal(first), 'Size loop did not exit')
                u.PostMessageW(first, 0x10, 0, 0)  # WM_CLOSE
                time.sleep(1.5)
                check(process.poll() is None and windows() == [second],
                      'Closing one window exited process or recreated the window')
                u.PostMessageW(second, 0x10, 0, 0)
                process.wait(timeout=10)
                check(process.returncode == 0, f'Bad exit code: {process.returncode}')
                print('Resize, multi-window close, final clean exit: OK')
            except Exception:
                output.flush()
                print((root / 'stdout.log').read_text(encoding='utf-8', errors='replace')[-4000:])
                raise
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
