# 一键退出程序的工具 (Ctrl+Q)

> [中文版](./README.md)  [english](./README_en.md)

一个轻量级 Windows 工具，用 Ctrl+Q 关闭当前前台程序，可替代系统的 Alt+F4。

## 功能特点

- **一键关闭**：关闭当前前台窗口，同进程的其它窗口不受影响
- **不卡输入**：键盘钩子只做按键判断，关闭动作交给独立工作线程
- **智能关闭**：先发 `WM_CLOSE` 优雅关闭，超时才强制结束
- **多层保护**：跳过关键系统进程、桌面/任务栏窗口；识别不出进程就不动手
- **托盘常驻**：右键托盘图标可停用/启用（图标同步变灰）、编辑配置、退出
- **单实例**：重复启动不会装两个钩子
- **配置外置**：热键、超时时间、额外保护名单都在 `ctrlq.ini`

## 使用方法

1. 双击 `ctrlq.exe`，托盘出现图标
2. 在任意窗口按 `Ctrl+Q`，该窗口所属的进程会被关闭
3. 右键托盘图标：
   - 停用 / 启用
   - 编辑配置（`ctrlq.ini`）
   - 退出

## 配置文件

首次运行会在 exe 同目录生成 `ctrlq.ini`（UTF-16 编码，记事本可直接编辑）：

```ini
[ctrlq]
; Ctrl/Alt/Shift/Win + 单个字母、数字，或 F1-F24
Hotkey=Ctrl+Q

; 优雅关闭的等待时间（毫秒，200-30000）
GracefulTimeout=3000

; 是否允许关闭资源管理器窗口（1=允许，0=禁止）
AllowShellClose=1

; 额外保护的进程名，逗号分隔
Protected=
```

修改后需重启程序生效。若把 `Hotkey` 写成非法值（例如没有修饰键的裸键），会自动回落到 `Ctrl+Q`。

## 开机自启

Windows 的用户态程序实际都是**登录后**启动。把 `ctrlq.exe` 放进当前用户的启动文件夹即可：

1. `Win+R` 输入 `shell:startup` 回车，打开启动文件夹
2. 把 `ctrlq.exe` 的**快捷方式**（不是 exe 本身）放进去
3. 下次登录自动启动

说明：

- 不需要管理员权限，不会弹 UAC。
- 快捷方式指向**固定路径**，移动或重命名 exe 后要重建。
- `ctrlq.ini` 按 exe 所在目录查找，自启不影响配置读取。
- 单实例锁是 `Local\` 命名空间（按登录会话），多用户各自登录互不影响。

## 编译

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

> `/utf-8` 不能省，否则源码里的中文菜单文本会按系统代码页解析成乱码。

### 托盘图标

`ctrlq.rc` 把两个图标编进 exe：资源 ID 1 为启用态（蓝色徽章 + 窗口轮廓 + 红色关闭角标），
ID 2 为停用态（去色变灰）。右键「停用 / 启用」时图标会同步切换，浅色和深色任务栏都清晰。

每个 ico 内含 16/20/24/32/40/48/64/128/256 共九档 32bpp 尺寸；16/20/24 三档单独加粗了
线条和叉号，避免缩到托盘大小糊成一团。设计源码是 `make_icon.py`（需要 Pillow），
改配色后执行 `python make_icon.py` 即可重新生成两个 ico 和 `icon_preview.png`。

> 不带资源文件编译也能运行，只是托盘图标会回落到系统默认的 `IDI_APPLICATION`。
>
> 若 `windres` 报 `cannot execute 'cc1'`，是 PATH 里的 gcc 为链接垫片、找不到同目录的
> `libexec`；把对应 MinGW 的 `mingw64/bin` 放到 PATH 最前面即可。

## 安全特性

关闭前依次通过三层检查，任一不通过就直接放弃：

1. **系统关键进程**：`IsProcessCritical()`（Windows 8.1+），由内核判定
2. **内置保护名单**：`dwm.exe`、`sihost.exe`、`taskhostw.exe`、`ShellExperienceHost.exe`、
   `StartMenuExperienceHost.exe`、`ctfmon.exe`、`audiodg.exe`、`fontdrvhost.exe`、
   `csrss.exe`、`winlogon.exe`、`lsass.exe` 等，外加 `Protected=` 自定义名单
3. **窗口准入**：必须是可见的顶层窗口、非 `WS_EX_TOOLWINDOW`、有标题，
   且类名不是 `Progman` / `WorkerW` / `Shell_TrayWnd` 等桌面与任务栏窗口

进程按风险分三档处理：

| 档位 | 对象 | 行为 |
|---|---|---|
| `Deny` | 关键进程、内置名单、`Protected=` | 直接放弃 |
| `GracefulOnly` | `explorer.exe` | 只发 `WM_CLOSE`，**永不强杀** |
| `Allow` | 其他进程 | `WM_CLOSE`，超时才强杀 |

`explorer.exe` 同时托管桌面、任务栏和文件夹窗口，所以额外加了两道限制：只放行
`CabinetWClass` / `ExploreWClass` 的文件夹窗口，并用 `GetShellWindow()` 排除桌面窗口。
`AllowShellClose=0` 可以让它彻底回到 `Deny` 档。

另外：读不到进程路径时**直接放弃**，不会拿着未知 PID 去杀。

## 已知限制

- **管理员窗口**：普通权限运行的本程序收不到高完整性窗口的按键，也无法结束它们。
  需要处理这类窗口时，以管理员身份运行 `ctrlq.exe`。
- **强制结束会丢数据**：程序不响应 `WM_CLOSE`（例如弹出"是否保存"对话框）时，
  等待 `GracefulTimeout` 后会被强杀。
- **无标题窗口**：部分全屏游戏、UWP 外壳窗口没有标题，会被跳过。
- **资源管理器不会强杀**：`explorer.exe` 只做优雅关闭。窗口卡住时 Ctrl+Q 会无反应，
  这是刻意的——强杀会闪掉桌面和任务栏，并丢失所有已打开的文件夹窗口。
- **资源管理器多标签**：Windows 11 中一个窗口的多个标签会被一起关掉，属于应用自身行为。

## 技术细节

```
键盘钩子（主线程）  只判断按键，命中后 PostMessage，立即 return 1 吞掉按键
    ↓
隐藏窗口（主线程）  收到 WM_APP_KILL，SetEvent 唤醒工作线程
    ↓
工作线程            OpenProcess(QUERY_LIMITED|SYNCHRONIZE|TERMINATE) 一次拿到句柄
                    ↓ 句柄不关，PID 不会被复用
                    QueryFullProcessImageNameW → 分档判定（Deny / GracefulOnly / Allow）
                    ↓
                    WM_CLOSE → 轮询目标窗口是否消失 → 仅 Allow 档才 TerminateProcess
```

关键点：低层键盘钩子有 `LowLevelHooksTimeout`（默认 300ms~1s），回调里做耗时操作会被
Windows **静默卸载**且不做任何提示。所以钩子内只允许 `PostMessage`，关闭流程全部在
工作线程里跑。

关闭的判据是**目标窗口消失**，不是进程退出：同一个 exe 开了多个窗口时，关掉其中一个窗口后
进程通常还活着，这不代表失败，不会再去强杀它。只有窗口在 `GracefulTimeout` 内始终没消失
（应用卡死或根本没处理 `WM_CLOSE`）才会结束整个进程。少数应用自己把"关闭任一窗口"实现成
"退出整个程序"，那属于应用行为，工具无法干预。

## 免责声明

本工具仅供学习和合法用途使用。使用前请保存好工作。因使用本工具造成的数据丢失或系统问题，作者不负责。

## 版本历史

### v1.1.2
- 支持关闭资源管理器文件夹窗口：`explorer.exe` 拆出 `GracefulOnly` 档，只做优雅关闭
- 该档额外限制为 `CabinetWClass` / `ExploreWClass` 窗口，桌面与任务栏不受影响
- 新增 `AllowShellClose` 开关，可整体禁用这一行为
- 新增 `CloseAppWindow(HWND)` 入口，便于测试单个窗口的关闭行为

### v1.1.1
- 关闭判据从"进程退出"改为"目标窗口消失"：同进程的多个窗口不再被一起关掉
- 托盘图标区分启用/停用两种状态（资源 ID 1 / 2）

### v1.1.0
- 钩子回调不再阻塞：关闭流程移入工作线程，修复"按一次后快捷键失效"
- 进程识别失败时直接放弃，不再假定安全
- 全程持有进程句柄，消除 PID 复用导致的误杀
- 改用 `QueryFullProcessImageNameW` + `PROCESS_QUERY_LIMITED_INFORMATION`
- 保护策略改为 `IsProcessCritical` + 窗口准入 + 可配置名单
- 命中后吞掉按键，不再与前台程序自带的 Ctrl+Q 冲突
- 忽略合成按键与自动重复；修饰键状态自行维护
- 新增托盘图标、单实例、`ctrlq.ini` 配置

### v1.0.0
- 初始版本：全局钩子 + WM_CLOSE/强制终止
