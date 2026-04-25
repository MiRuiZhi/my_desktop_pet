#pragma once
#include <windows.h>

namespace stealth {

// 用户态隐藏
void HideFromTaskManager();
void DisableConsole();
void SpoofProcessName(const wchar_t* name);

// 反调试
bool IsBeingDebugged();
void AntiDebug_Init();

// 反虚拟机
bool IsRunningInVM();

// 内存保护
void LockMemory(void* ptr, size_t size);
void UnlockMemory(void* ptr, size_t size);
void SecureClear(void* ptr, size_t size);

// GUI防捕获
void MakeWindowUncapturable(HWND hWnd);

}
