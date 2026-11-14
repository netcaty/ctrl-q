# One-Click Exit Program Tool (Ctrl+Q)

> [中文版](./README.md)  [english](./README_en.md)

A lightweight Windows utility that closes the foreground program with Ctrl+Q — an alternative to Alt+F4.

## Features

- **One key close**: closes the foreground window; other windows of the same process stay open
- **Never blocks input**: the keyboard hook only classifies the key and returns immediately; the close runs on a worker thread
- **Graceful first**: sends `WM_CLOSE`, and only force-terminates after a timeout
- **Layered protection**: skips critical system processes, desktop/taskbar windows, and refuses anything it cannot identify
- **Lives in the tray**: right-click to disable/enable, edit config, or exit
- **Single instance**: no duplicated hooks from double-launching
- **External config**: hotkey, timeout, and extra protection list live in `ctrlq.ini`

## Usage

1. Double-click `ctrlq.exe`; a tray icon appears
2. Press `Ctrl+Q` in any window to close the process behind it
3. Right-click the tray icon to disable/enable, edit `ctrlq.ini`, or exit

## Configuration

On first run `ctrlq.ini` is created next to the exe (UTF-16, editable in Notepad):

```ini
[ctrlq]
; Ctrl/Alt/Shift/Win + a letter, a digit, or F1-F24
Hotkey=Ctrl+Q

; milliseconds to wait after WM_CLOSE (200-30000)
GracefulTimeout=3000

; extra protected process names, comma separated
Protected=
```

Restart the app after editing. An invalid `Hotkey` (e.g. no modifier) falls back to `Ctrl+Q`.

## Start at logon

User-mode apps on Windows start at **logon**. Put `ctrlq.exe` into your Startup folder:

1. Press `Win+R`, type `shell:startup`, hit Enter
2. Put a **shortcut** to `ctrlq.exe` (not the exe itself) into that folder
3. It starts on your next logon

Notes:

- No admin rights and no UAC prompt.
- The shortcut points at a **fixed path**; recreate it after moving or renaming the exe.
- `ctrlq.ini` is located next to the exe, so autostart does not change config resolution.
- The single-instance mutex lives in the `Local\` namespace (per logon session), so multiple users
  can each run their own copy.

## Building

### MinGW / GCC
```bash
windres ctrlq.rc -O coff -o ctrlq_res.o
g++ -o ctrlq.exe main.cpp ctrlq_res.o -luser32 -lshell32 -O2 -mwindows -static -std=c++11
```

### MSVC
```bat
rc /fo ctrlq.res ctrlq.rc
cl /EHsc /utf-8 /O2 main.cpp ctrlq.res /Fe:ctrlq.exe user32.lib shell32.lib /link /SUBSYSTEM:WINDOWS
```

> `/utf-8` is required, otherwise the Chinese menu strings are decoded with the system code page.

### Tray icon

`ctrlq.rc` embeds two icons: resource ID 1 for the enabled state (blue badge + window outline
+ red close badge), ID 2 for the disabled state (desaturated grey). The icon switches together
with the "Enable / Disable" menu item, and stays legible on both light and dark taskbars.

Each .ico holds nine 32bpp sizes (16/20/24/32/40/48/64/128/256). The 16/20/24 variants use
thicker strokes so the shape survives at tray size. The design source is `make_icon.py`
(requires Pillow); run `python make_icon.py` to regenerate both .ico files and `icon_preview.png`.

> Building without the resource script still works, but the tray falls back to the stock
> `IDI_APPLICATION` icon.
>
> If `windres` fails with `cannot execute 'cc1'`, the gcc on PATH is a shim that cannot find its
> `libexec` directory; put the matching MinGW `mingw64/bin` first on PATH.

## Safety

Three checks run in order; failing any one aborts the operation:

1. **Critical process**: `IsProcessCritical()` (Windows 8.1+), decided by the kernel
2. **Built-in guard list**: `explorer.exe`, `dwm.exe`, `sihost.exe`, `taskhostw.exe`,
   `ShellExperienceHost.exe`, `StartMenuExperienceHost.exe`, `ctfmon.exe`, `audiodg.exe`,
   `fontdrvhost.exe`, `csrss.exe`, `winlogon.exe`, `lsass.exe`, plus your `Protected=` entries
3. **Window admission**: the target must be a visible top-level window with a title, not a
   `WS_EX_TOOLWINDOW`, and not a shell window (`Progman`, `WorkerW`, `Shell_TrayWnd`, ...)

If the process path cannot be read, the tool **gives up** instead of killing an unknown PID.

## Known limitations

- **Elevated windows**: a non-elevated instance cannot receive keystrokes while an elevated
  window is focused, nor terminate it. Run `ctrlq.exe` as administrator for those.
- **Data loss**: if the app ignores `WM_CLOSE` (e.g. shows a save prompt), it is force-killed
  after `GracefulTimeout`.
- **Windows without a title**: some fullscreen games and UWP shell windows are skipped.

## How it works

```
keyboard hook (main thread)   classify the key, PostMessage, return 1 to swallow it
    ↓
hidden window (main thread)   on WM_APP_KILL: SetEvent, nothing else
    ↓
worker thread                 OpenProcess(QUERY_LIMITED|SYNCHRONIZE|TERMINATE) once
                              ↓ the handle pins the object, so the PID cannot be recycled
                              QueryFullProcessImageNameW → three checks
                              ↓
                              WM_CLOSE → poll until the window is gone → TerminateProcess
```

Success is measured by **the target window disappearing**, not by the process exiting. When one exe
owns several windows, closing one of them usually leaves the process alive — that is a success, not
a failure, and it is not force-killed. Only a window that is still there after `GracefulTimeout`
(hung app, or no `WM_CLOSE` handler) takes the whole process down. A few apps implement "close any
window" as "quit the app"; that is the app's behaviour and the tool cannot change it.

The important part: low-level hooks have a `LowLevelHooksTimeout` (300ms–1s by default). A slow
callback makes Windows **silently uninstall** the hook with no notification. Therefore the hook
only posts a message; everything else happens on the worker thread.

## Disclaimer

For learning and legitimate use only. Save your work first. The author is not responsible for
data loss or system issues caused by this tool.

## Version history

### v1.1.1
- Success criterion changed from "process exited" to "target window gone", so sibling windows are no longer closed
- Tray icon now reflects enabled/disabled state (resource IDs 1 / 2)

### v1.1.0
- Hook callback no longer blocks: close logic moved to a worker thread (fixes "works once, then dies")
- Fails closed when the process cannot be identified
- Holds a single process handle for the whole operation, eliminating PID-reuse kills
- Uses `QueryFullProcessImageNameW` + `PROCESS_QUERY_LIMITED_INFORMATION`
- Protection reworked: `IsProcessCritical` + window admission + configurable list
- Swallows the hotkey instead of forwarding it to the foreground app
- Ignores injected keys and auto-repeat; modifier state is tracked manually
- Added tray icon, single instance, and `ctrlq.ini`

### v1.0.0
- Initial release: global hook + WM_CLOSE / force terminate
