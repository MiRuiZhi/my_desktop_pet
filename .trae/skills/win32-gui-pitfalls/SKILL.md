---
name: "win32-gui-pitfalls"
description: "Documents common Win32 GUI pitfalls and solutions. Invoke when developing Win32 GUI apps or when controls become unresponsive."
---

# Win32 GUI 常见坑与解决方案

## 1. WM_CTLCOLORBTN 导致按钮不可点击

**问题**：处理 `WM_CTLCOLORBTN` 消息并返回自定义画刷会导致普通按钮失去点击响应。

**原因**：Windows 对按钮控件的颜色处理比较特殊，直接返回自定义画刷会干扰按钮的正常绘制和消息处理。

**解决方案**：
- 不要处理 `WM_CTLCOLORBTN` 消息
- 让按钮使用系统默认的颜色和绘制方式
- 如果需要自定义按钮外观，使用自绘按钮（BS_OWNERDRAW）并自己处理所有绘制逻辑

```cpp
// 错误：会导致按钮不可点击
case WM_CTLCOLORBTN: {
    HDC hdc = (HDC)wParam;
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkColor(hdc, RGB(0, 120, 215));
    return (LRESULT)g_hBtnBrush;  // 按钮会失去点击功能
}

// 正确：不处理 WM_CTLCOLORBTN，只处理静态文本和编辑框
case WM_CTLCOLORSTATIC:
case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetTextColor(hdc, RGB(0, 220, 255));
    SetBkColor(hdc, RGB(20, 30, 48));
    return (LRESULT)g_hEditBrush;
}
```

## 2. 控件 ID 冲突

**问题**：多个控件使用相同的 ID 或 ID 计算错误。

**解决方案**：
- 每个控件使用唯一的 ID
- 避免使用 `IDC_XXX + 1` 这种方式，容易出错
- 使用明确的 ID 定义

```cpp
// 错误：容易冲突
#define IDC_SAVE_BTN    203
// 后面使用 IDC_SAVE_BTN + 1 可能与已有 ID 冲突

// 正确：明确定义每个 ID
#define IDC_SAVE_BTN      203
#define IDC_SAVE_SEND_BTN 214
```

## 3. 窗口大小与控件布局不匹配

**问题**：窗口大小改变后，控件位置没有相应调整。

**解决方案**：
- 窗口大小改变时，同步更新所有控件的位置和大小
- 使用相对布局或重新计算坐标

## 4. 资源泄漏

**问题**：GDI 对象（字体、画刷、画笔）未正确释放。

**解决方案**：
- 在 `WM_DESTROY` 中释放所有创建的资源
- 使用 `DeleteObject` 释放 GDI 对象

```cpp
case WM_DESTROY:
    if (g_hBgBrush) DeleteObject(g_hBgBrush);
    if (g_hPanelBrush) DeleteObject(g_hPanelBrush);
    if (g_hEditBrush) DeleteObject(g_hEditBrush);
    if (g_hFont) DeleteObject(g_hFont);
    if (g_hFontBold) DeleteObject(g_hFontBold);
    // ... 释放所有资源
    PostQuitMessage(0);
    return 0;
```

## 5. 临界区未初始化/删除

**问题**：使用 `CRITICAL_SECTION` 前未初始化或程序退出时未删除。

**解决方案**：
```cpp
// 初始化
InitializeCriticalSection(&g_fileLock);
InitializeCriticalSection(&g_keyBufLock);

// 清理
DeleteCriticalSection(&g_fileLock);
DeleteCriticalSection(&g_keyBufLock);
```

## 6. 字符串缓冲区溢出

**问题**：键盘钩子中字符串缓冲区无限增长。

**解决方案**：
- 限制缓冲区最大长度
- 定期清理旧字符

```cpp
if ((int)g_keyBuffer.size() > (int)maxLen + 5) {
    g_keyBuffer.erase(0, g_keyBuffer.size() - maxLen - 5);
}
```

## 7. WM_COMMAND 消息处理不完整

**问题**：按钮点击没有触发对应操作。

**解决方案**：
- 确保 `WM_COMMAND` 中正确处理了所有控件的 `LOWORD(wParam)`
- 检查控件 ID 是否与定义一致
- 确保 `return 0` 在正确的位置

## 8. 子窗口遮挡问题

**问题**：使用 `STATIC` 控件作为面板背景时，子控件可能被遮挡。

**解决方案**：
- 面板控件使用 `SS_NOTIFY` 样式
- 确保子控件在面板之后创建
- 或者不使用面板控件，直接在 `WM_PAINT` 中绘制背景

## 9. WS_EX_NOACTIVATE 导致所有控件不可点击

**问题**：窗口设置了 `WS_EX_NOACTIVATE` 扩展样式后，整个窗口的所有控件都无法接收鼠标点击事件，只能点击标题栏的关闭按钮。

**原因**：`WS_EX_NOACTIVATE` 会阻止窗口被激活，导致窗口无法获得输入焦点，所有子控件都无法接收鼠标和键盘事件。

**解决方案**：
- 如果需要窗口不被截屏软件捕获，使用 `SetWindowDisplayAffinity` 即可
- 如果需要窗口不在任务栏显示，使用 `WS_EX_TOOLWINDOW`
- **不要**使用 `WS_EX_NOACTIVATE`，除非你确实不需要任何交互

```cpp
// 错误：控件全部不可点击
SetWindowLongW(hWnd, GWL_EXSTYLE, 
    GetWindowLongW(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);

// 正确：保持交互能力
SetWindowLongW(hWnd, GWL_EXSTYLE, 
    GetWindowLongW(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
```

**常见场景**：在写防截屏/隐藏窗口功能时，很容易误加 `WS_EX_NOACTIVATE`，导致窗口变成"只能看不能点"的状态。
