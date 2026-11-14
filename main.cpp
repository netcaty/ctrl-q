// main.cpp - 全局键盘钩子版本 v1.1.1
//
// 架构：
//   键盘钩子（主线程）  -> 只判断按键，命中后 PostMessage，立即返回
//   隐藏窗口（主线程）  -> 收到 WM_APP_KILL，SetEvent 唤醒工作线程
//   工作线程            -> 识别窗口/进程，优雅关闭，超时强杀
//
// 编译（先编图标资源）：
//   windres ctrlq.rc -O coff -o ctrlq_res.o
//   g++ -o ctrlq.exe main.cpp ctrlq_res.o -luser32 -lshell32 -O2 -mwindows -static -std=c++11
//   rc /fo ctrlq.res ctrlq.rc
//   cl /EHsc /utf-8 /O2 main.cpp ctrlq.res /Fe:ctrlq.exe user32.lib shell32.lib /link /SUBSYSTEM:WINDOWS

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cwchar>
#include <cwctype>
#include <cstring>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

// ---------------------------------------------------------------- 常量

static const UINT WM_APP_KILL = WM_APP + 1;   // 钩子 -> 隐藏窗口
static const UINT WM_APP_TRAY = WM_APP + 2;   // 托盘回调

static const UINT ID_TRAY_TOGGLE = 1001;
static const UINT ID_TRAY_CONFIG = 1002;
static const UINT ID_TRAY_EXIT = 1003;

// 会话/外壳级进程：不在 IsProcessCritical 范围内，但杀掉会破坏桌面或输入
static const wchar_t* const kBuiltinProtected[] = {
    L"csrss.exe", L"wininit.exe", L"winlogon.exe", L"services.exe",
    L"lsass.exe", L"smss.exe", L"userinit.exe", L"logonui.exe",
    L"explorer.exe", L"dwm.exe", L"sihost.exe", L"taskhostw.exe",
    L"shellexperiencehost.exe", L"startmenuexperiencehost.exe",
    L"searchhost.exe", L"searchui.exe", L"textinputhost.exe",
    L"ctfmon.exe", L"audiodg.exe", L"fontdrvhost.exe",
    L"runtimebroker.exe", L"smartscreen.exe", L"msmpeng.exe",
    L"system", L"registry", L"memory compression",
};

struct Hotkey {
    UINT vk = 'Q';
    bool ctrl = true;
    bool alt = false;
    bool shift = false;
    bool win = false;
};

// ---------------------------------------------------------------- 全局状态

static HINSTANCE g_hInst = nullptr;
static HWND      g_hwndSink = nullptr;
static HHOOK     g_hHook = nullptr;
static HANDLE    g_hKillEvent = nullptr;   // 自动复位：待处理一次关闭
static HANDLE    g_hQuitEvent = nullptr;   // 手动复位：退出信号
static HANDLE    g_hWorker = nullptr;
static HANDLE    g_hMutex = nullptr;

static Hotkey    g_hotkey;
static int       g_gracefulMs = 3000;
static std::vector<std::wstring> g_extraProtected;

static bool      g_enabled = true;
static bool      g_hotkeyHeld = false;     // 用于吞掉自动重复
static bool      g_trayAdded = false;
static UINT      g_msgTaskbarCreated = 0;

// 修饰键由自己维护，不依赖 GetAsyncKeyState
static bool g_lctl = false, g_rctl = false;
static bool g_lshift = false, g_rshift = false;
static bool g_lalt = false, g_ralt = false;
static bool g_lwin = false, g_rwin = false;

// ---------------------------------------------------------------- 小工具

static bool EqualsNoCase(const wchar_t* a, const wchar_t* b) {
    return _wcsicmp(a, b) == 0;
}

static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L".";
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *slash = 0;
    return path;
}

static std::wstring IniPath() {
    return ExeDir() + L"\\ctrlq.ini";
}

// ---------------------------------------------------------------- 热键解析

static bool ParseHotkey(const std::wstring& text, Hotkey& out) {
    const std::wstring s = Trim(text);
    if (s.empty()) return false;

    Hotkey hk;
    hk.ctrl = hk.alt = hk.shift = hk.win = false;
    hk.vk = 0;

    size_t pos = 0;
    for (;;) {
        size_t plus = s.find(L'+', pos);
        std::wstring tok = Trim(s.substr(pos, plus == std::wstring::npos ? std::wstring::npos : plus - pos));
        if (tok.empty()) return false;

        std::wstring low;
        for (size_t i = 0; i < tok.size(); ++i)
            low += (wchar_t)towlower(tok[i]);

        if (low == L"ctrl" || low == L"control") {
            hk.ctrl = true;
        } else if (low == L"alt") {
            hk.alt = true;
        } else if (low == L"shift") {
            hk.shift = true;
        } else if (low == L"win" || low == L"windows") {
            hk.win = true;
        } else if (low.size() == 1 &&
                   ((low[0] >= L'a' && low[0] <= L'z') || (low[0] >= L'0' && low[0] <= L'9'))) {
            if (hk.vk) return false;
            hk.vk = (UINT)towupper(low[0]);
        } else if (low.size() >= 2 && low[0] == L'f') {
            int n = _wtoi(low.c_str() + 1);
            if (n < 1 || n > 24 || hk.vk) return false;
            hk.vk = VK_F1 + (UINT)(n - 1);
        } else {
            return false;
        }

        if (plus == std::wstring::npos) break;
        pos = plus + 1;
    }

    if (!hk.vk) return false;
    // 不允许裸键：至少一个修饰键，避免把普通按键吞掉
    if (!hk.ctrl && !hk.alt && !hk.shift && !hk.win) return false;

    out = hk;
    return true;
}

static std::wstring HotkeyText() {
    std::wstring s;
    if (g_hotkey.ctrl)  s += L"Ctrl+";
    if (g_hotkey.alt)   s += L"Alt+";
    if (g_hotkey.shift) s += L"Shift+";
    if (g_hotkey.win)   s += L"Win+";

    if (g_hotkey.vk >= VK_F1 && g_hotkey.vk <= VK_F24) {
        s += L"F" + std::to_wstring(g_hotkey.vk - VK_F1 + 1);
    } else if ((g_hotkey.vk >= L'0' && g_hotkey.vk <= L'9') ||
               (g_hotkey.vk >= L'A' && g_hotkey.vk <= L'Z')) {
        s += (wchar_t)g_hotkey.vk;
    } else {
        s += L"0x" + std::to_wstring(g_hotkey.vk);
    }
    return s;
}

// ---------------------------------------------------------------- 配置

static void WriteDefaultIni(const std::wstring& path) {
    std::wstring t =
        L"; ctrlq 配置文件（修改后需重启程序生效）\r\n"
        L"[ctrlq]\r\n"
        L"; 关闭前台程序的热键：Ctrl/Alt/Shift/Win + 单个字母、数字，或 F1-F24\r\n"
        L"Hotkey=Ctrl+Q\r\n"
        L"\r\n"
        L"; 先发 WM_CLOSE 优雅关闭，超时后才强制结束（毫秒，200-30000）\r\n"
        L"GracefulTimeout=3000\r\n"
        L"\r\n"
        L"; 额外保护的进程名，逗号分隔，例如 chrome.exe,notepad.exe\r\n"
        L"Protected=\r\n";

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;

    const WORD bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(f, &bom, sizeof(bom), &written, nullptr);
    WriteFile(f, t.c_str(), (DWORD)(t.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
}

static void LoadConfig() {
    g_hotkey = Hotkey();
    g_gracefulMs = 3000;
    g_extraProtected.clear();

    const std::wstring path = IniPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        WriteDefaultIni(path);

    wchar_t buf[512] = {};
    GetPrivateProfileStringW(L"ctrlq", L"Hotkey", L"Ctrl+Q", buf, ARRAYSIZE(buf), path.c_str());
    Hotkey hk;
    if (ParseHotkey(buf, hk)) g_hotkey = hk;

    int t = GetPrivateProfileIntW(L"ctrlq", L"GracefulTimeout", 3000, path.c_str());
    if (t < 200) t = 200;
    if (t > 30000) t = 30000;
    g_gracefulMs = t;

    wchar_t prot[1024] = {};
    GetPrivateProfileStringW(L"ctrlq", L"Protected", L"", prot, ARRAYSIZE(prot), path.c_str());
    std::wstring s(prot);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t sep = s.find_first_of(L",;", pos);
        std::wstring item = Trim(s.substr(pos, sep == std::wstring::npos ? std::wstring::npos : sep - pos));
        if (!item.empty()) g_extraProtected.push_back(item);
        if (sep == std::wstring::npos) break;
        pos = sep + 1;
    }
}

// ---------------------------------------------------------------- 进程保护判断

// Win8.1+：由内核判断该进程是否为关键进程；老系统上指针为空则跳过
typedef BOOL (WINAPI *PfnIsProcessCritical)(HANDLE, PBOOL);

static PfnIsProcessCritical GetIsProcessCritical() {
    static PfnIsProcessCritical fn = (PfnIsProcessCritical)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "IsProcessCritical");
    return fn;
}

static bool IsProtectedName(const wchar_t* base) {
    for (size_t i = 0; i < ARRAYSIZE(kBuiltinProtected); ++i)
        if (EqualsNoCase(base, kBuiltinProtected[i])) return true;
    for (size_t i = 0; i < g_extraProtected.size(); ++i)
        if (EqualsNoCase(base, g_extraProtected[i].c_str())) return true;
    return false;
}

// 只接受"普通应用窗口"：可见、非工具窗口、顶层、有标题、不是桌面/任务栏
static bool IsAppWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) return false;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
    if (GetWindowTextLengthW(hwnd) == 0) return false;

    wchar_t cls[128] = {};
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls))) {
        static const wchar_t* const kShellCls[] = {
            L"Progman", L"WorkerW", L"Shell_TrayWnd",
            L"Shell_SecondaryTrayWnd", L"NotifyIconOverflowWindow",
        };
        for (size_t i = 0; i < ARRAYSIZE(kShellCls); ++i)
            if (EqualsNoCase(cls, kShellCls[i])) return false;
    }
    return true;
}

// ---------------------------------------------------------------- 关闭逻辑

// 判据是"这个窗口还在不在"，不是"进程还在不在"。
// 同时校验 PID，避免窗口销毁后 HWND 被系统回收给别的进程，误判成"还活着"。
static bool WindowStillAlive(HWND hwnd, DWORD pid) {
    if (!IsWindow(hwnd)) return false;
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    return owner == pid;
}

static void KillForeground() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsAppWindow(hwnd)) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) return;

    // 一次 OpenProcess 拿到句柄，后续全程复用：
    // 句柄未关闭前内核对象不会被回收，因此不存在 PID 复用的风险。
    DWORD access = PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE;
    HANDLE h = OpenProcess(access, FALSE, pid);
    bool canTerminate = (h != nullptr);
    if (!h) {
        // 高完整性进程拿不到 TERMINATE，退化为只做优雅关闭
        h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        canTerminate = false;
    }
    if (!h) return;   // 连身份都读不到 -> 放弃，绝不盲杀

    wchar_t path[MAX_PATH] = {};
    DWORD len = ARRAYSIZE(path);
    if (!QueryFullProcessImageNameW(h, 0, path, &len)) { CloseHandle(h); return; }

    const wchar_t* base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;

    if (IsProtectedName(base)) { CloseHandle(h); return; }

    PfnIsProcessCritical isCritical = GetIsProcessCritical();
    if (isCritical) {
        BOOL critical = FALSE;
        if (isCritical(h, &critical) && critical) { CloseHandle(h); return; }
    }

    // 只关"这一个窗口"：成功后进程可能还活着（同一个 exe 的其它窗口），
    // 那是应用自己的事，不能因为进程没退出就把它的所有窗口一起杀掉。
    const DWORD budget = (DWORD)g_gracefulMs;
    const DWORD start = GetTickCount();

    if (IsWindow(hwnd)) {
        DWORD sendTimeout = budget / 2;
        if (sendTimeout < 200) sendTimeout = 200;
        SendMessageTimeoutW(hwnd, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            sendTimeout, nullptr);
    }

    // 等目标窗口消失；窗口没了 = 成功，进程留不留都无所谓
    for (;;) {
        if (!WindowStillAlive(hwnd, pid)) { CloseHandle(h); return; }
        if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) { CloseHandle(h); return; }

        const DWORD elapsed = GetTickCount() - start;
        if (elapsed >= budget) break;

        DWORD slice = budget - elapsed;
        if (slice > 50) slice = 50;
        WaitForSingleObject(h, slice);
    }

    // 窗口一直没消失 = 应用没响应 WM_CLOSE（卡住或无窗口过程），才强杀进程
    if (canTerminate) TerminateProcess(h, 0);
    CloseHandle(h);
}

static DWORD WINAPI WorkerProc(LPVOID) {
    HANDLE waits[2] = { g_hKillEvent, g_hQuitEvent };
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (r != WAIT_OBJECT_0) break;           // 收到退出信号
        ResetEvent(g_hKillEvent);
        KillForeground();
    }
    return 0;
}

// ---------------------------------------------------------------- 键盘钩子

static bool CtrlDown()  { return g_lctl || g_rctl; }
static bool ShiftDown() { return g_lshift || g_rshift; }
static bool AltDown()   { return g_lalt || g_ralt; }
static bool WinDown()   { return g_lwin || g_rwin; }

static void TrackModifier(DWORD vk, bool down) {
    switch (vk) {
    case VK_LCONTROL: g_lctl = down; break;
    case VK_RCONTROL: g_rctl = down; break;
    case VK_LSHIFT:   g_lshift = down; break;
    case VK_RSHIFT:   g_rshift = down; break;
    case VK_LMENU:    g_lalt = down; break;
    case VK_RMENU:    g_ralt = down; break;
    case VK_LWIN:     g_lwin = down; break;
    case VK_RWIN:     g_rwin = down; break;
    default: break;
    }
}

static bool ModifiersMatch() {
    return CtrlDown() == g_hotkey.ctrl
        && AltDown() == g_hotkey.alt
        && ShiftDown() == g_hotkey.shift
        && WinDown() == g_hotkey.win;
}

// 钩子必须极快返回：这里只做判断 + PostMessage，绝不碰进程句柄、绝不 Sleep。
// 一旦超过 LowLevelHooksTimeout，Windows 会静默卸载钩子且不做任何提示。
static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION)
        return CallNextHookEx(g_hHook, nCode, wParam, lParam);

    const KBDLLHOOKSTRUCT* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    TrackModifier(k->vkCode, down);

    const bool matched = (k->vkCode == g_hotkey.vk) && ModifiersMatch();

    if (g_enabled && down && matched) {
        if (k->flags & LLKHF_INJECTED)           // 忽略合成按键
            return CallNextHookEx(g_hHook, nCode, wParam, lParam);

        if (g_hotkeyHeld) return 1;              // 自动重复：吞掉但不重复触发
        g_hotkeyHeld = true;

        if (g_hwndSink) PostMessageW(g_hwndSink, WM_APP_KILL, 0, 0);
        return 1;                                // 吞掉，不传给前台程序
    }

    if (up || !matched) g_hotkeyHeld = false;
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------- 托盘

static UINT TrayIconId() { return 1; }

// 资源 ID 1 = 启用，2 = 停用（见 ctrlq.rc）。
// LoadIcon 返回共享句柄，由系统管理，不要 DestroyIcon。
static HICON TrayIconHandle() {
    HICON h = LoadIconW(g_hInst, MAKEINTRESOURCEW(g_enabled ? 1 : 2));
    if (!h) h = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION
    return h;
}

static std::wstring TrayTipText() {
    return L"ctrlq - " + HotkeyText() + (g_enabled ? L" 关闭前台程序" : L"（已停用）");
}

static void UpdateTrayTip() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndSink;
    nid.uID = TrayIconId();
    nid.uFlags = NIF_TIP;
    const std::wstring tip = TrayTipText();
    wcsncpy(nid.szTip, tip.c_str(), ARRAYSIZE(nid.szTip) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void UpdateTrayIcon() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndSink;
    nid.uID = TrayIconId();
    nid.uFlags = NIF_ICON;
    nid.hIcon = TrayIconHandle();
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void AddTrayIcon() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndSink;
    nid.uID = TrayIconId();
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = TrayIconHandle();

    const std::wstring tip = TrayTipText();
    wcsncpy(nid.szTip, tip.c_str(), ARRAYSIZE(nid.szTip) - 1);

    g_trayAdded = (Shell_NotifyIconW(NIM_ADD, &nid) != FALSE);
}

static void RemoveTrayIcon() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndSink;
    nid.uID = TrayIconId();
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

static void ToggleEnabled() {
    g_enabled = !g_enabled;
    if (!g_enabled) g_hotkeyHeld = false;
    UpdateTrayIcon();
    UpdateTrayTip();
}

static void OpenConfig() {
    const std::wstring path = IniPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        WriteDefaultIni(path);
    ShellExecuteW(nullptr, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
}

static void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    const std::wstring toggle = g_enabled ? L"停用" : L"启用";
    const std::wstring info = L"当前快捷键：" + HotkeyText();
    AppendMenuW(menu, MF_STRING, ID_TRAY_TOGGLE, toggle.c_str());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, info.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_CONFIG, L"编辑配置（ctrlq.ini）");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出");

    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwndSink);

    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                    pt.x, pt.y, 0, g_hwndSink, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case ID_TRAY_TOGGLE: ToggleEnabled(); break;
    case ID_TRAY_CONFIG: OpenConfig(); break;
    case ID_TRAY_EXIT:   DestroyWindow(g_hwndSink); break;
    default: break;
    }
}

// ---------------------------------------------------------------- 隐藏窗口

static LRESULT CALLBACK SinkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // explorer 重启后托盘图标会消失，需要重新添加
    if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {
        g_trayAdded = false;
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_APP_KILL:
        SetEvent(g_hKillEvent);                  // 只做这一件事，立刻返回
        return 0;

    case WM_APP_TRAY: {
        const UINT ev = LOWORD(lParam);
        if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) ShowTrayMenu();
        else if (ev == WM_LBUTTONDBLCLK) ToggleEnabled();
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------- 入口

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    g_hInst = hInstance;

    // 单实例：重复启动会装两个钩子，一次按键杀两次
    g_hMutex = CreateMutexW(nullptr, TRUE, L"Local\\ctrlq_single_instance_v1");
    if (!g_hMutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"ctrlq 已经在运行。\n\n请到任务栏托盘图标上右键操作。",
                    L"ctrlq", MB_ICONINFORMATION | MB_OK);
        return 0;
    }

    // 启动时按键可能已经被按住，先同步一次修饰键状态
    g_lctl = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    g_rctl = (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
    g_lshift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    g_rshift = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    g_lalt = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
    g_ralt = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
    g_lwin = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0;
    g_rwin = (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

    LoadConfig();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SinkProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CtrlQ.Sink";
    if (!RegisterClassExW(&wc)) return 1;

    g_hwndSink = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"ctrlq",
                                 WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!g_hwndSink) return 1;

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    g_hKillEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_hQuitEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hKillEvent || !g_hQuitEvent) return 1;

    g_hWorker = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
    if (!g_hWorker) return 1;

    g_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, hInstance, 0);
    if (!g_hHook) {
        wchar_t msg[256] = {};
        wsprintfW(msg, L"安装键盘钩子失败（错误码 %lu）。", GetLastError());
        MessageBoxW(nullptr, msg, L"ctrlq", MB_ICONERROR | MB_OK);
        SetEvent(g_hQuitEvent);
        WaitForSingleObject(g_hWorker, 2000);
        return 1;
    }

    AddTrayIcon();

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_hHook);
    RemoveTrayIcon();
    SetEvent(g_hQuitEvent);
    WaitForSingleObject(g_hWorker, 5000);

    CloseHandle(g_hWorker);
    CloseHandle(g_hKillEvent);
    CloseHandle(g_hQuitEvent);
    if (g_hMutex) CloseHandle(g_hMutex);
    return 0;
}
