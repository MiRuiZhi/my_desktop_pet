# 高安全截屏服务 - C++ Windows 原生版

按 **Ctrl+Shift+Alt+P** 触发静默截屏，加密保存到本地。

---

## 项目结构

```
├── CMakeLists.txt          # 用户态程序构建
├── src/
│   ├── main.cpp            # 入口 + 热键注册
│   ├── screenshot.cpp      # BitBlt 截屏
│   ├── hotkey.cpp          # RegisterHotKey 全局热键
│   ├── crypto.cpp          # Windows CNG AES-256-GCM 加密
│   └── stealth.cpp         # 进程隐藏 + 反调试 + 反虚拟机
├── include/
│   ├── screenshot.h
│   ├── hotkey.h
│   ├── crypto.h
│   └── stealth.h
└── driver/
    ├── hideproc.c          # DKOM 内核态进程隐藏驱动
    ├── hideproc.vcxproj    # 驱动构建项目
    └── build_driver.bat    # 驱动构建脚本
```

---

## 构建用户态程序

### 环境要求

- Windows 10/11
- Visual Studio 2019/2022（C++ 桌面开发工作负载）
- CMake 3.15+

### 构建步骤

```cmd
cd C:\path\to\project

mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

生成文件：`build\Release\screenshot_svc.exe`

---

## 构建内核态驱动（可选，最高隐蔽级别）

### 环境要求

- Visual Studio 2022
- Windows Driver Kit (WDK) 10
- 测试签名模式已启用

### 启用测试签名（管理员 CMD）

```cmd
bcdedit /set testsigning on
```

重启电脑生效。

### 构建步骤

1. 双击 `driver\build_driver.bat`
2. 或在 "x64 Native Tools Command Prompt" 中：

```cmd
cd driver
msbuild hideproc.vcxproj /p:Configuration=Release /p:Platform=x64
```

生成文件：`driver\x64\Release\hideproc.sys`

### 安装驱动（管理员 CMD）

```cmd
sc create HideProc binPath= C:\full\path\to\hideproc.sys type= kernel
sc start HideProc
```

### 卸载驱动

```cmd
sc stop HideProc
sc delete HideProc
```

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
