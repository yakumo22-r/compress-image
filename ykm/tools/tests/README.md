# Windows backend regression

Requires an interactive Windows desktop, xmake/MSVC, and Python 3. Tests open and close their own windows. Do not interact with those windows during the modal-loop measurement.

Use a non-embedded host build (`ykm_app` empty):

```sh
xmake f -p windows -a x64 -m debug --ykm_app="" -y
xmake build ykm
xmake build ykm-test-win32-sys
xmake run ykm-test-win32-sys
xmake build ykm-test-d3d11-surface
xmake run ykm-test-d3d11-surface
xmake build ykm-test-host-surface
xmake run ykm-test-host-surface
python tools/tests/windows_runtime.py
python tools/tests/windows_worker.py
npm run verify:cli
```

For another build directory/configuration, pass `--exe path/to/ykm.exe` to the Python test.

## Coverage

- `win32_sys.cpp`: framebuffer dimensions equal native client pixels after resize; Chinese BMP character; UTF-16 surrogate pair; invalid surrogate replacement; focus-loss reset; WM_UNICHAR query/scalar validation; left-double-click button identity. Does not link ImGui.
  Also verifies exactly-once retirement, with the published surface withdrawn and the old HWND still valid inside the callback, for both owned and external destruction.
- `d3d11_surface.cpp`: verifies swapchain reuse, zero-size pause, resize, explicit render-target unbinding, same-handle generation changes, reattachment to another HWND, and absent/incompatible surface detach. This tests DX11 resources, not complete ImGui/native lifecycle recovery.
- `host_surface.cpp`: verifies real HWND destruction detaches the ImGui Win32 backend and DX11 window resources, preserves the logical host, and restores the previous current context. A test-supplied replacement HWND resumes drawing with the same ImGui context, renderer backend, and Session. It does not exercise automatic native-window recreation.
- `windows_runtime.py`: creates temporary widgets, checks two native windows, repeated resize, native SC_SIZE modal state and continuing JS render calls, closing one window without recreation, clean exit after closing the last window. Temporary files and the test process are cleaned up.
- `windows_worker.py`: verifies UI frames continue during a worker's JS busy loop, JSON snapshot delivery, inbox backpressure, module startup with timers, filesystem completion from a Promise job, stop/restart, and normal window close with a busy worker.
- CLI regression: existing core API checks.

The modal assertion uses the Windows GUI_INMOVESIZE flag, not just an elapsed frame count. Desktop interaction or focus changes can terminate the modal loop and fail the test.

## Manual acceptance still required

- Run the sys test on actual 100%, 150%, and 200% displays; its output reports the DPI tested. A pass at DPI 96 does not verify high-DPI configurations.
- Move windows between displays with different DPI; check back-buffer sharpness and pointer/control alignment.
- Type Chinese in ImGui input controls; check candidate position, commit, backspace, focus switching, and absence of duplicated text.
- Check Emoji display and JS textinput consumers. Synthetic WM_CHAR tests verify scalar events, not a complete IME composition protocol.
- Check visual smoothness while dragging a border. Automated frame-count tests do not inspect rendered pixels.
