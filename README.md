# GyroXInput - Android Gyro to Virtual DS4 Controller

[![Language](https://img.shields.io/badge/Language-C%2B%2B-blue.svg)](https://isocpp.org/) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## 概述

GyroXInput 是一个 Windows 应用程序，作为将 Android 设备的传感器数据用作 PC 游戏输入的解决方案的一部分。它的主要功能是读取由其他进程写入共享内存的陀螺仪和加速度计数据，并使用 ViGEmBus 将这些数据模拟为一个虚拟的 DualShock 4 (DS4) 控制器。这使得在支持 DS4 运动控制器的游戏中使用 Android 设备的陀螺仪进行瞄准或其他运动控制成为可能。

本项目是以下项目的**下游组件**：

1.  **LowLatencyInput (@https://github.com/shuakami/LowLatencyInput):** 这是运行在 Android 设备上的应用程序，负责捕获触摸、陀螺仪和加速度计数据，并通过 TCP 网络协议将其低延迟地发送到 PC。
2.  **android_usb_listener (@https://github.com/shuakami/android_usb_listener):** (推测) 这个组件很可能作为 PC 端的服务运行，负责接收来自 `LowLatencyInput` 的 TCP 网络数据流，解析数据，并将其写入 Windows 共享内存 (`Global\\GyroSharedMemory`)，同时通过事件 (`Global\\GyroDataUpdatedEvent`) 通知 GyroXInput 有新数据。

**因此，GyroXInput 并不直接与 Android 设备或 USB 通信，而是依赖于像 `android_usb_listener` 这样的中间桥接服务来提供共享内存中的数据。**

## 工作原理

1.  **数据源 (Android):** `LowLatencyInput` 应用在 Android 设备上运行，捕获陀螺仪、加速度计等传感器数据，并将数据打包通过 TCP 发送到 PC 端的指定 IP 地址和端口 (默认为 `127.0.0.1:12345`，通常需要配合 `adb reverse`)。
2.  **数据桥接 (PC):** 一个 PC 端服务（例如 `android_usb_listener`）监听指定的 TCP 端口，接收来自 `LowLatencyInput` 的数据包。该服务解析数据包，提取出陀螺仪和加速度计信息。
3.  **共享内存写入:** 该桥接服务将解析后的传感器数据写入 Windows 上的一个命名共享内存区域 (`Global\\GyroSharedMemory`)。写入完成后，它会触发一个命名事件 (`Global\\GyroDataUpdatedEvent`) 来通知等待的进程。
4.  **GyroXInput 读取与模拟:**
    *   本程序 (`gyro_client.exe`) 运行在 Windows 上，启动后会连接到上述的共享内存区域和命名事件。
    *   程序等待 `Global\GyroDataUpdatedEvent` 事件被触发。
    *   当事件触发时，`GyroXInput` 从共享内存中安全地读取最新的陀螺仪 (X, Y, Z) 和加速度计 (X, Y, Z) 数据以及瞄准状态 (`is_aiming`)。
    *   它使用 [ViGEmBus](https://github.com/ViGEm/ViGEmBus) 驱动程序创建一个虚拟的 DS4 控制器（如果尚未创建）。
    *   读取到的传感器数据被实时映射到虚拟 DS4 控制器的运动传感器输入（可能根据 `is_aiming` 状态决定是否激活映射）。
5.  **游戏交互:** 支持 DS4 运动控制输入的游戏识别到这个由 ViGEmBus 创建的虚拟控制器，并接收来自 `GyroXInput` 模拟的运动数据，从而实现使用手机陀螺仪进行游戏内操作。

## 功能特性

*   将 Android 设备的陀螺仪/加速度计数据实时映射到虚拟 DS4 控制器。
*   使用高效的共享内存和事件机制进行跨进程通信。
*   利用 ViGEmBus 驱动程序进行可靠的控制器模拟。
*   包含瞄准状态检测，可能用于选择性激活陀螺仪。
*   包含优雅退出处理 (Ctrl+C)。

## 系统要求

*   **Windows 操作系统:** 需要安装 ViGEmBus 驱动程序。
*   **ViGEmBus 驱动:** 从 [ViGEmBus Releases](https://github.com/ViGEm/ViGEmBus/releases) 下载并安装最新版本。
*   **配套 Android 应用:** 需要 `LowLatencyInput` 和 `android_usb_listener` (或等效功能的应用) 在 Android 设备上运行并通过 USB 连接到 PC。

## 构建

本项目使用 CMake 构建。

1.  **安装 CMake:** 确保你的系统安装了 CMake ([https://cmake.org/](https://cmake.org/))。
2.  **安装 C++ 编译器:** 需要一个支持 C++11 的编译器 (例如 Visual Studio Build Tools)。
3.  **获取 ViGEmClient:** 克隆或下载 ViGEmClient SDK 到项目目录（或确保 CMake 能找到它）。`CMakeLists.txt` 中包含 `add_subdirectory(ViGEmClient)`，表明它期望 `ViGEmClient` 源代码位于子目录中。
4.  **配置和构建:**
    ```bash
    # 创建构建目录
    mkdir build
    cd build

    # 配置项目
    cmake ..

    # 构建项目
    cmake --build . --config Release
    ```
    编译后的可执行文件 `gyro_client.exe` 将位于 `build/Release` (或 `build/Debug`) 目录下。

## 使用方法

1.  **安装 ViGEmBus 驱动程序。**
2.  **在 Android 设备上设置并运行 `LowLatencyInput` 和 `android_usb_listener`** (或等效应用)，并确保通过 USB 连接到 PC 且数据正在传输。
3.  **运行 `gyro_client.exe`。** 程序启动后，它将尝试连接到共享内存和 ViGEmBus，并创建一个虚拟 DS4 控制器。
4.  **启动游戏:** 确保游戏配置为使用 DS4 控制器并启用了运动控制功能。

**注意:** `gyro_client.exe` 需要与写入共享内存的进程（如 `android_usb_listener`）同时运行。

## 贡献

欢迎通过 Pull Requests 或 Issues 提出改进建议。

## 许可证

本项目采用 [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0) 许可证](https://creativecommons.org/licenses/by-nc-sa/4.0/)。