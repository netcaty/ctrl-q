# One-Click Exit Program Tool (Ctrl+Q)

> [中文版](./README.md)  [english](./README_en.md)

A lightweight Windows tool for quickly exiting the current active window. Binds to the Ctrl+Q hotkey as an alternative to the system's ALT+F4.

## Features

- **Quick Termination**: One-click termination of the process for the current active window
- **Safety Protection**: Automatically avoids terminating critical system processes
- **Intelligent Closure**: Attempts graceful closure first, then forces termination
- **Portable Operation**: No installation required, single executable file

## Usage

- Double-click the .exe to launch
- It will run in the background. To exit, use Task Manager to find the `ctrlq` process and end it.

## Compilation Guide

### Using MinGW/GCC
```bash
# Run in msys2 ucrt64 terminal
g++ -o ctrlq.exe main.cpp -luser32 -lpsapi -O2 -mwindows -static
```

### Using MSVC
```bash
# Run in Visual Studio Developer Command Prompt
cl /EHsc /O2 main.cpp /Fe:ctrlq.exe user32.lib psapi.lib /link /SUBSYSTEM:WINDOWS
```

## Safety Features

The program automatically protects the following critical system processes:
- `csrss.exe` - Windows Client Server Runtime Process
- `wininit.exe` - Windows Initialization Process
- `winlogon.exe` - Windows Logon Application
- `services.exe` - Service Control Manager
- `lsass.exe` - Local Security Authority Subsystem Service
- `smss.exe` - Session Manager Subsystem
- `System` - System Idle Process
- `explorer.exe` - Windows Explorer

## Exit Codes

- `0`: Success (Process terminated)
- `1`: Failed to get process information
- `2`: Attempted to terminate a critical system process
- `3`: Attempted to terminate itself
- `4`: Failed to terminate the process

## Troubleshooting

### Common Issues

**Can't terminate certain processes?**
Some protected processes require administrator privileges. Run the program as Administrator, or use the `/f` flag (use with caution).

**The program flashes and disappears immediately?**
When compiled with `-mwindows` (MinGW) or `/SUBSYSTEM:WINDOWS` (MSVC), the program runs silently without a console window. This is normal behavior.

## Technical Details

### How It Works
1. Registers a keyboard hook to listen for the Ctrl+Q key combination
2. Uses `GetForegroundWindow()` to get the current focused window
3. Gets the Process ID via `GetWindowThreadProcessId()`
4. Attempts to send a `WM_CLOSE` message for graceful window closure
5. If the window doesn't close, forcefully terminates the process

## Alternatives

### AutoHotkey Version
```autohotkey
; Save as ctrlq.ahk
^q::
    WinGet, active_pid, PID, A
    Process, Close, %active_pid%
return
```

## Disclaimer

This tool is for learning and legitimate use only. The author is not responsible for any data loss, system damage, or other issues caused by using this tool. Please save your work and use with caution.

## Building from Source

### Prerequisites
- C++ compiler (GCC/MinGW or MSVC)
- Windows SDK (for Windows headers)

### Build Script
The project includes a `build.bat` script for easy compilation on Windows systems.

## Source Code

The main source file is `main.cpp`. Key functions:
- `WinMain()`: Windows application entry point
- `GetRealForegroundWindow()`: Gets the active window
- `TerminateProcessByPid()`: Terminates a process by its ID
- `GetProcessName()`: Gets the executable name from a process ID

## Contribution Guidelines

Issues and Pull Requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

## Version History

### v1.0.0
- Initial release
- Basic process termination functionality
- Critical process protection
- Multiple operation modes