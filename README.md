# 一键退出程序的工具 (Ctrl+Q)

> [中文版](./README.md)  [english](./README_en.md)

一个轻量级Windows工具，用于快速退出当前活动窗口。绑定Ctrl+Q快捷键，可替代系统的ALT+F4键。

## 功能特点

- **快速终止**：一键终止当前活动窗口的进程
- **安全保护**：自动避免终止关键系统进程
- **智能关闭**：先尝试正常关闭，再强制终止
- **便携运行**：无需安装，单个可执行文件

## 使用方法

- 双击exe启动
- 在后台常驻内存，退出用任务管理器查找ctrlq进程然后杀死

## 编译指南

### 使用 MinGW/GCC
```bash
# 在msys2 ucrt64终端运行
g++ -o ctrlq.exe main.cpp -luser32 -lpsapi -O2 -mwindows -static
```

### 使用 MSVC
```bash
# 在 Visual Studio 开发者命令提示符中运行
cl /EHsc /O2 main.cpp /Fe:ctrlq.exe user32.lib psapi.lib /link /SUBSYSTEM:WINDOWS
```

## 安全特性

程序会自动保护以下关键系统进程：
- `csrss.exe` - Windows客户端服务器运行时进程
- `wininit.exe` - Windows初始化进程
- `winlogon.exe` - Windows登录应用程序
- `services.exe` - 服务控制管理器
- `lsass.exe` - 本地安全认证子系统进程
- `smss.exe` - 会话管理器子系统
- `System` - 系统空闲进程
- `explorer.exe` - Windows资源管理器

## 退出代码

- `0`：成功（进程已终止）
- `1`：无法获取进程信息
- `2`：尝试终止关键系统进程
- `3`：尝试终止自身
- `4`：终止进程失败

## 故障排除

### 常见问题

**无法终止某些进程？**
某些受保护的进程需要管理员权限。请以管理员身份运行程序，或使用 `/f` 标志（谨慎使用）。

**程序一闪而过就消失了？**
使用 `-mwindows`（MinGW）或 `/SUBSYSTEM:WINDOWS`（MSVC）编译时，程序会以无控制台窗口的方式静默运行，这是正常现象。

## 技术细节

### 工作原理
1. 注册键盘钩子，监听ctrl+q按键
2. 使用 `GetForegroundWindow()` 获取当前焦点窗口
3. 通过 `GetWindowThreadProcessId()` 获取进程ID
4. 尝试发送 `WM_CLOSE` 消息正常关闭窗口
5. 如果窗口未关闭，则强制终止进程

## 替代方案

### AutoHotkey 版本
```autohotkey
; 保存为 ctrlq.ahk
^q::
    WinGet, active_pid, PID, A
    Process, Close, %active_pid%
return
```

## 免责声明

本工具仅供学习和合法用途使用。使用本工具造成的任何数据丢失、系统损坏或其他问题，作者概不负责。使用前请保存好工作，谨慎操作。

## 从源代码构建

### 前提条件
- C++编译器（GCC/MinGW 或 MSVC）
- Windows SDK（用于Windows头文件）

### 构建脚本
项目中包含一个 `build.bat` 脚本，可在Windows系统上轻松编译。

## 源代码

主源文件为 `main.cpp`。关键函数：
- `WinMain()`：Windows应用程序入口点
- `GetRealForegroundWindow()`：获取活动窗口
- `TerminateProcessByPid()`：通过ID终止进程
- `GetProcessName()`：从进程ID获取可执行文件名称

## 贡献指南

欢迎提交 Issue 和 Pull Request。对于重大改动，请先开 Issue 讨论您想要更改的内容。

## 版本历史

### v1.0.0
- 初始版本
- 基本的进程终止功能
- 关键进程保护
- 多种操作模式