#pragma once
#include <windows.h>
#include <functional>

namespace hotkey {

class HotkeyManager {
public:
    bool Register(HWND hWnd, int id, UINT modifiers, UINT vk, std::function<void()> callback);
    void Unregister(int id);

private:
    struct HotkeyEntry {
        int id;
        std::function<void()> callback;
    };
    HotkeyEntry entries[8];
    int count = 0;
};

}
