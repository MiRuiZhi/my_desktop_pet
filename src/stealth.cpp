#include "stealth.h"
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <cstdio>

namespace stealth {

typedef NTSTATUS(NTAPI* pNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

void HideFromTaskManager() {
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (hKernel32) {
        typedef DWORD(WINAPI* pRegisterServiceProcess)(DWORD, DWORD);
        pRegisterServiceProcess pRSP = (pRegisterServiceProcess)GetProcAddress(hKernel32, "RegisterServiceProcess");
        if (pRSP) {
            pRSP(GetCurrentProcessId(), 1);
        }
    }
}

void DisableConsole() {
    HWND hConsole = GetConsoleWindow();
    if (hConsole) {
        ShowWindow(hConsole, SW_HIDE);
    }
}

void SpoofProcessName(const wchar_t* name) {
    wchar_t mutexName[MAX_PATH];
    swprintf(mutexName, MAX_PATH, L"Global\\%s", name);
    CreateMutexW(NULL, FALSE, mutexName);
}

bool IsBeingDebugged() {
    if (IsDebuggerPresent()) return true;

    BOOL bDebug = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &bDebug);
    if (bDebug) return true;

    PPEB pPeb = (PPEB)__readgsqword(0x60);
    if (pPeb->BeingDebugged) return true;

    unsigned int* pNtGlobalFlag = (unsigned int*)((unsigned char*)pPeb + 0xBC);
    if (*pNtGlobalFlag & 0x70) return true;

    return false;
}

void AntiDebug_Init() {
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(GetCurrentThread(), &ctx);

    ctx.Dr0 = 0;
    ctx.Dr1 = 0;
    ctx.Dr2 = 0;
    ctx.Dr3 = 0;
    ctx.Dr7 = 0;

    SetThreadContext(GetCurrentThread(), &ctx);
}

bool IsRunningInVM() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VMware, Inc.\\VMware Tools", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\VBoxMouse", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Virtual Machine\\Guest\\Parameters", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }

    return false;
}

void LockMemory(void* ptr, size_t size) {
    VirtualLock(ptr, size);
}

void UnlockMemory(void* ptr, size_t size) {
    VirtualUnlock(ptr, size);
}

void SecureClear(void* ptr, size_t size) {
    SecureZeroMemory(ptr, size);
}

void MakeWindowUncapturable(HWND hWnd) {
    typedef BOOL(WINAPI* pSetWindowDisplayAffinity)(HWND, DWORD);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        pSetWindowDisplayAffinity pSWDA = (pSetWindowDisplayAffinity)GetProcAddress(hUser32, "SetWindowDisplayAffinity");
        if (pSWDA) {
            pSWDA(hWnd, 0x00000011);
        }
    }
    
    SetWindowLongW(hWnd, GWL_EXSTYLE, GetWindowLongW(hWnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
    
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

}
