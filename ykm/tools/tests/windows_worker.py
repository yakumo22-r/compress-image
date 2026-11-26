"""Exercise real QuickJS worker threads using the non-embedded Windows host."""

import argparse
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', default='build/windows/x64/debug/ykm.exe')
    exe = Path(parser.parse_args().exe).resolve()
    user32 = c.WinDLL('user32')
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user32.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    with tempfile.TemporaryDirectory(prefix='ykm-worker-') as directory:
        root = Path(directory)
        (root / 'ykm.sessions').write_text('ui\tui\t1\tui.js\nworker\tworker\t0\tworker.js\n')
        (root / 'payload.txt').write_text('worker input')
        (root / 'worker.js').write_text('''/* ykm:module */
import { basename } from 'path';
import { readFile } from 'fs';
await new Promise(resolve => setTimeout(resolve, 10));
if (ykm.ui !== undefined || ykm.tools !== undefined || basename('a/b') !== 'b')
    throw Error('incorrect worker API');
let count = 0;
globalThis.onMessage = (message, sender) => {
    if (message.value !== 42) throw Error('message was not copied');
    if (message.loop) {
        ykm.send(sender, {kind: 'busy', count: ++count});
        while (true) {}
    }
    Promise.resolve().then(async () => {
        const input = await readFile('payload.txt', 'utf8');
        if (input !== 'worker input') throw Error('worker file read failed');
        ykm.send(sender, {kind: 'reply', count: ++count});
    });
};
''')
        (root / 'ui.js').write_text('''
ykm.logging.enable('worker.log');
const win = new ykm.ui.Window({key: 'worker-test', title: 'Worker regression'});
let frames = 0;
let baseline = 0;
let phase = 'starting';
if (!ykm.sessions.start('worker')) throw Error('cannot start worker');
function send(loop) {
    const payload = {value: 42, loop};
    ykm.send('worker', payload);
    payload.value = 99;
}
function render() {
    ++frames;
    if (phase === 'starting' && ykm.sessions.isRunning('worker')) {
        phase = 'first busy';
        send(true);
    }
    if (phase === 'restarting' && ykm.sessions.start('worker')) phase = 'waiting restart';
    if (phase === 'waiting restart' && ykm.sessions.isRunning('worker')) {
        phase = 'waiting reply';
        send(false);
    }
    if (phase === 'proving frames' && frames >= baseline + 5) {
        if (!ykm.sessions.stop('worker') || ykm.sessions.isRunning('worker')) throw Error('stop failed');
        phase = 'restarting';
    }
    return win;
}
function onMessage(message, sender) {
    if (sender !== 'worker') throw Error('incorrect sender');
    if (phase === 'first busy' && message.kind === 'busy' && message.count === 1) {
        let full = false;
        try { for (let i = 0; i < 300; ++i) send(false); }
        catch (e) { full = e instanceof RangeError; }
        if (!full) throw Error('worker inbox did not apply backpressure');
        baseline = frames;
        phase = 'proving frames';
    } else if (phase === 'waiting reply' && message.kind === 'reply' && message.count === 1) {
        phase = 'final busy';
        send(true);
    } else if (phase === 'final busy' && message.kind === 'busy' && message.count === 2) {
        ykm.print('WORKER_TEST_READY_TO_CLOSE');
    } else { throw Error('unexpected worker reply'); }
}
''')
        with (root / 'stdout.log').open('wb') as output:
            process = subprocess.Popen([str(exe), str(root)], cwd=root, stdout=output, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 12
                log = root / 'worker.log'
                while time.monotonic() < deadline and process.poll() is None:
                    if log.exists() and 'WORKER_TEST_READY_TO_CLOSE' in log.read_text(errors='replace'):
                        break
                    time.sleep(.05)
                else:
                    raise AssertionError('Worker did not complete phase checks')
                windows = []

                @callback_type
                def visit(hwnd, _):
                    pid = w.DWORD()
                    user32.GetWindowThreadProcessId(hwnd, c.byref(pid))
                    if pid.value == process.pid:
                        windows.append(hwnd)
                    return True

                user32.EnumWindows(visit, 0)
                if not any(user32.PostMessageW(hwnd, 0x10, 0, 0) for hwnd in windows):
                    raise AssertionError('Could not post WM_CLOSE')
                if process.wait(timeout=5) != 0:
                    raise AssertionError(f'Incorrect exit code: {process.returncode}')
                print('Worker: independent UI frames, JSON messages, bounded queue, timers, restart and busy shutdown: OK')
            except Exception:
                print((root / 'stdout.log').read_text(errors='replace')[-4000:])
                raise
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)


if __name__ == '__main__':
    main()
