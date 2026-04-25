# 高安全截屏服务 - C++ Windows 原生版

按 **Ctrl+Shift+Alt+P** 触发静默截屏，加密保存到本地。

---

## 一键构建（Windows 上操作）

### 步骤 1：安装前置依赖

1. **Visual Studio 2019/2022**
   - 下载：https://visualstudio.microsoft.com/downloads/
   - 安装时勾选 **"使用 C++ 的桌面开发"**

2. **CMake 3.15+**
   - 下载：https://cmake.org/download/
   - 安装时勾选 **"Add CMake to the system PATH"**

### 步骤 2：构建用户态程序

双击 `build.bat`，等待构建完成。

生成文件：`screenshot_svc.exe`

### 步骤 3：安装内核驱动（可选，最高隐蔽级别）

1. 启用测试签名（管理员 CMD）：
   ```cmd
   bcdedit /set testsigning on
   ```
   重启电脑。

2. 右键 `install_driver.bat` → **以管理员身份运行**

---

## 使用方式

### 启动服务

双击 `screenshot_svc.exe`，程序在后台静默运行，无窗口。

### 触发截屏

在任意窗口按 **Ctrl+Shift+Alt+P**

### 查看截图

截图保存在：
```
%LOCALAPPDATA%\screenshot-service\screenshots\
```

格式为 `.enc` 加密文件。

---

## 安全特性

| 特性 | 实现方式 |
|------|---------|
| 静默截屏 | BitBlt API，无窗口弹出 |
| 全局热键 | RegisterHotKey API，系统级 |
| 内存加密 | Windows CNG AES-256-GCM |
| 进程隐藏 | DKOM 驱动从 EPROCESS 链表 unlink |
| 反调试 | IsDebuggerPresent + PEB 检测 + 硬件断点检测 |
| 反虚拟机 | 注册表检测 VMware/VirtualBox/Hyper-V/WSL |
| 内存锁定 | VirtualLock 防止交换到磁盘 |
| 敏感数据清除 | SecureZeroMemory 清零 |

---

## 注意事项

1. **杀毒软件**：由于使用了进程隐藏和反调试技术，可能被杀毒软件标记。需要添加白名单。
2. **驱动签名**：内核驱动需要测试签名模式，生产环境需要 EV 代码签名证书。
3. **权限要求**：驱动安装需要管理员权限，用户态程序普通权限即可。
