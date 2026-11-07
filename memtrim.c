#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")

typedef LONG NTSTATUS;
typedef NTSTATUS (WINAPI *PFN_NtSetSystemInformation)(ULONG, PVOID, ULONG);

#define SYS_MEMORY_LIST_INFO  80
#define MEM_EMPTY_WORKINGSETS 2
#define MEM_FLUSH_MODIFIED    3
#define MEM_PURGE_STANDBY     4

static BOOL IsElevated(void)
{
    HANDLE tok;
    TOKEN_ELEVATION te;
    DWORD cb = 0;
    BOOL r = FALSE;
    te.TokenIsElevated = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        if (GetTokenInformation(tok, TokenElevation, &te, sizeof te, &cb))
            r = (te.TokenIsElevated != 0);
        CloseHandle(tok);
    }
    return r;
}

static BOOL EnablePrivilege(const WCHAR *name)
{
    HANDLE tok;
    BOOL ok = FALSE;
    LUID luid;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return FALSE;
    if (LookupPrivilegeValueW(NULL, name, &luid)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (AdjustTokenPrivileges(tok, FALSE, &tp, sizeof tp, NULL, NULL))
            ok = (GetLastError() == ERROR_SUCCESS);
    }
    CloseHandle(tok);
    return ok;
}

static unsigned long long AvailBytes(void)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    return GlobalMemoryStatusEx(&ms) ? ms.ullAvailPhys : 0;
}

int WINAPI WinMain(HINSTANCE hIns, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    WCHAR msg[1024];
    WCHAR tail[160];
    WCHAR self[MAX_PATH];
    HMODULE ntdll;
    PFN_NtSetSystemInformation ntSet;
    LONG ret[3];
    int i;
    int retCode;
    unsigned long long before, after, delta;
    unsigned int beforeMB, afterMB, deltaMB, took = 0;
    LARGE_INTEGER freq, t0, t1;
    BOOL privOk;



    if (strstr(lpCmd, "--go") == NULL) {
        retCode = MessageBoxW(NULL,
            L"即将执行一次内存优化。\n\n"
            L"它会做三件事：\n"
            L"· 清空所有程序占用的物理内存工作集\n"
            L"· 把待写回的数据刷到磁盘\n"
            L"· 清空系统文件缓存\n\n"
            L"执行后的几分钟内，重新打开程序或读取文件会变慢，之后会自行恢复。\n\n"
            L"确定要现在优化吗？",
            L"内存优化",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 |
            MB_SETFOREGROUND | MB_TOPMOST);
        if (retCode != IDYES)
            return 0;
    }



    if (!IsElevated()) {
        GetModuleFileNameW(NULL, self, MAX_PATH);
        if ((INT_PTR)ShellExecuteW(NULL, L"runas", self, L"--go", NULL, SW_SHOWNORMAL) > 32)
            return 0;
        MessageBoxW(NULL,
            L"内存优化需要管理员权限。\n\n"
            L"你刚刚取消了 UAC 授权，本次操作已取消。",
            L"内存优化", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        return 1;
    }

    privOk = EnablePrivilege(L"SeProfileSingleProcessPrivilege");

    ntdll = GetModuleHandleW(L"ntdll.dll");
    ntSet = (ntdll != NULL)
        ? (PFN_NtSetSystemInformation)(void *)GetProcAddress(ntdll, "NtSetSystemInformation")
        : NULL;

    ret[0] = ret[1] = ret[2] = 0x7FFFFFFF;

    if (privOk && ntSet != NULL) {
        before = AvailBytes();
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);

        for (i = 0; i < 3; i++) {
            ULONG cmd = (ULONG)(MEM_EMPTY_WORKINGSETS + i);
            ret[i] = ntSet(SYS_MEMORY_LIST_INFO, &cmd, (ULONG)sizeof cmd);
        }

        QueryPerformanceCounter(&t1);
        if (freq.QuadPart > 0)
            took = (unsigned int)((t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart);

        Sleep(300);
        after = AvailBytes();
    } else {
        before = 0;
        after = 0;
    }

    delta = (after > before) ? (after - before) : 0;
    beforeMB = (unsigned int)(before / 1024 / 1024);
    afterMB = (unsigned int)(after / 1024 / 1024);
    deltaMB = (unsigned int)(delta / 1024 / 1024);

    if (!privOk || ntSet == NULL) {
        MessageBoxW(NULL,
            L"优化未能执行。\n\n"
            L"原因：无法启用 SeProfileSingleProcessPrivilege 特权，或找不到 NtSetSystemInformation。\n"
            L"请确认是以管理员身份运行的。",
            L"内存优化", MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
        return 1;
    }

    wsprintfW(msg,
        L"优化前可用：%u MB\n"
        L"优化后可用：%u MB\n"
        L"本次增加：%u MB\n"
        L"本次耗时：%u ms\n\n"
        L"步骤 2 清工作集：0x%08X\n"
        L"步骤 3 刷已修改：0x%08X\n"
        L"步骤 4 清待机：0x%08X\n\n"
        L"（0x00000000 表示该步成功）",
        beforeMB, afterMB, deltaMB, took,
        (unsigned int)ret[0], (unsigned int)ret[1], (unsigned int)ret[2]);

    MessageBoxW(NULL, msg, L"内存优化",
                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
    return 0;
}
