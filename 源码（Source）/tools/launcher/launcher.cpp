// [ANDROID-FLASHER] 32/64 位启动器（32 位 exe）
// 作用：判断当前 Windows 位数，转去同目录下 app64\qFlipper.exe 或 app32\qFlipper.exe。
// 本身不涉及任何 Qt 代码，纯 Win32 API，静态编译，体积很小。

#include <windows.h>
#include <stdio.h>

static bool isWindows64()
{
    BOOL wow64 = FALSE;

    if (IsWow64Process(GetCurrentProcess(), &wow64) && wow64) {
        return true;
    }

    // 32 位进程在 64 位系统上还能从环境变量确认一次
    WCHAR arch[64] = {0};
    if (GetEnvironmentVariableW(L"PROCESSOR_ARCHITEW6432", arch, 63) > 0) {
        return true;
    }

    return false;
}

static void stripFirstArg(WCHAR *cmdLine)
{
    WCHAR *p = cmdLine;

    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') {
            ++p;
        }
        if (*p == L'"') {
            ++p;
        }
    } else {
        while (*p && *p != L' ') {
            ++p;
        }
    }

    while (*p == L' ') {
        ++p;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nShow)
{
    (void)hInstance;
    (void)hPrev;
    (void)lpCmdLine;
    (void)nShow;

    WCHAR exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    WCHAR dir[MAX_PATH] = {0};
    wcscpy_s(dir, MAX_PATH, exePath);

    WCHAR *lastSlash = wcsrchr(dir, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
    }

    const WCHAR *sub = isWindows64() ? L"app64" : L"app32";

    WCHAR target[MAX_PATH] = {0};
    wsprintfW(target, L"%s\\%s\\qFlipper.exe", dir, sub);

    if (GetFileAttributesW(target) == INVALID_FILE_ATTRIBUTES) {
        WCHAR msg[MAX_PATH * 2] = {0};
        wsprintfW(msg,
                  L"找不到 %s\\%s\\qFlipper.exe\n\n程序目录被改动过，请重新安装 qFlipper。",
                  dir, sub);
        MessageBoxW(NULL, msg, L"qFlipper", MB_ICONERROR | MB_OK);
        return 2;
    }

    WCHAR *cmdLine = GetCommandLineW();
    stripFirstArg(cmdLine);

    WCHAR fullCmd[MAX_PATH * 3] = {0};
    if (cmdLine && *cmdLine) {
        wsprintfW(fullCmd, L"\"%s\" %s", target, cmdLine);
    } else {
        wsprintfW(fullCmd, L"\"%s\"", target);
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    WCHAR workDir[MAX_PATH] = {0};
    wcscpy_s(workDir, MAX_PATH, target);
    WCHAR *slash = wcsrchr(workDir, L'\\');
    if (slash) {
        *slash = L'\0';
    }

    if (!CreateProcessW(target, fullCmd, NULL, NULL, FALSE, 0, NULL, workDir, &si, &pi)) {
        MessageBoxW(NULL, L"启动 qFlipper 失败。", L"qFlipper", MB_ICONERROR | MB_OK);
        return 3;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return 0;
}
