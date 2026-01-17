// keyboard_hook.cpp - 全局键盘钩子版本
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")

HHOOK g_hKeyboardHook = NULL;

// 系统关键进程列表
const std::vector<std::string> CRITICAL_PROCESSES = {
    "csrss.exe", "wininit.exe", "winlogon.exe", 
    "services.exe", "lsass.exe", "smss.exe", 
    "System", "System Idle Process", "explorer.exe"
};

void KillForegroundProcess();
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // 安装全局键盘钩子
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, hInstance, 0);
    
    if (!g_hKeyboardHook) {
        return 1;
    }
    
    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // 卸载钩子
    UnhookWindowsHookEx(g_hKeyboardHook);
    
    return 0;
}

// 键盘钩子过程
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKeyInfo = (KBDLLHOOKSTRUCT*)lParam;
        
        // 检测 Ctrl+Q 快捷键
        bool ctrlPressed = GetAsyncKeyState(VK_CONTROL) & 0x8000;
        //bool altPressed = GetAsyncKeyState(VK_MENU) & 0x8000;
        
        if (ctrlPressed  && pKeyInfo->vkCode == 'Q' && wParam == WM_KEYDOWN) {
            // 处理快捷键
            KillForegroundProcess();
        }
    }
    
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// 终止当前焦点进程
void KillForegroundProcess()
{
    HWND hWnd = GetForegroundWindow();
    if (!hWnd) return;
    
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == 0) return;
    
    // 获取进程名
    char procName[MAX_PATH] = {0};
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess) {
        GetModuleFileNameExA(hProcess, NULL, procName, sizeof(procName));
        CloseHandle(hProcess);
    }
    
    // 提取文件名
    char* pName = strrchr(procName, '\\');
    if (pName) pName++;
    else pName = procName;
    
    // 检查是否为关键进程
    for (const auto& critProc : CRITICAL_PROCESSES) {
        if (_stricmp(pName, critProc.c_str()) == 0) {
            return; // 不终止关键进程
        }
    }
    
    // 不终止自己
    if (pid == GetCurrentProcessId()) return;
    
    // 尝试正常关闭
    SendMessageTimeout(hWnd, WM_CLOSE, 0, 0, SMTO_BLOCK, 1000, NULL);
    Sleep(500);
    
    // 强制终止
    hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
    }
}