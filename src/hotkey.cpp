#include "hotkey.h"
#include <windows.h>

namespace hotkey {

bool HotkeyManager::Register(HWND hWnd, int id, UINT modifiers, UINT vk, std::function<void()> callback) {
    if (count >= 8) return false;

    if (!::RegisterHotKey(hWnd, id, modifiers, vk)) {
        return false;
    }

    entries[count].id = id;
    entries[count].callback = std::move(callback);
    count++;
    return true;
}

void HotkeyManager::Unregister(int id) {
    ::UnregisterHotKey(NULL, id);
    for (int i = 0; i < count; i++) {
        if (entries[i].id == id) {
            entries[i].callback = nullptr;
            break;
        }
    }
}

}
