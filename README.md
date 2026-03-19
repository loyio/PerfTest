# PerfTest - Windows 平台 C++ 性能优化教程

实用性能优化学习项目，包含可直接运行的 C++ 示例，带有详细中文注释。

> **声明**: 本项目仅用于性能优化技术的学习和演示。示例中的代码为了突出要点做了大量简化，实际工程中的性能问题远比这里复杂得多，涉及的架构、业务逻辑、多模块交互等因素远非单个 demo 能覆盖。请勿将本项目的实现直接用于生产环境。

## 项目结构

```
PerfTest/
├── CMakeLists.txt              # 顶层构建配置 (add_subdirectory)
├── common/                     # 公共工具库
│   ├── CMakeLists.txt
│   ├── profiler.h / .cpp       # 计时器、Benchmark 工具
│   ├── memory_tracker.h / .cpp # 内存追踪器
│   └── perf_case.h             # PerfCase 框架 + ADD_PERF_CASE 宏
│
├── memory_leak/                # 内存泄漏检测与修复
│   ├── CMakeLists.txt
│   ├── 01_basic_leak.cpp       # 基本泄漏模式与检测
│   ├── 02_smart_ptr_fix.cpp    # 智能指针解决方案 + 性能对比
│   ├── 03_circular_reference.cpp # 循环引用问题 + weak_ptr
│   └── 04_win_api_leak.cpp     # Windows HANDLE/GDI 泄漏 + RAII
│
├── stutter/                    # 卡顿优化
│   ├── CMakeLists.txt
│   ├── 01_cache_miss.cpp       # CPU 缓存未命中 + AoS vs SoA
│   ├── 02_false_sharing.cpp    # 伪共享 + 缓存行对齐
│   ├── 03_lock_contention.cpp  # 锁竞争 + 分片锁 + 读写锁
│   └── 04_blocking_io.cpp      # 阻塞I/O + 异步 + 内存映射
│
├── framerate/                  # 帧率优化
│   ├── CMakeLists.txt
│   ├── 01_game_loop.cpp        # 游戏循环策略 (固定/可变/插值)
│   ├── 02_frame_pacing.cpp     # 帧步调分析 + P99 帧时间
│   └── 03_object_pooling.cpp   # 对象池 vs 动态分配
│
├── win_perf/                   # Windows 性能工具
│   ├── CMakeLists.txt
│   ├── 01_etw_tracing.cpp      # ETW 追踪 + WPR/WPA 使用指南
│   └── 02_query_perf_counter.cpp # 高精度计时 + 测量方法论
│
├── PerfCaseGUI/                # ImGui 交互式性能测试平台
│   ├── CMakeLists.txt          # 自动收集 cases/*.cpp
│   ├── main.cpp                # DX11 + ImGui 主循环
│   └── cases/                  # 性能用例 (ADD_PERF_CASE 自动注册)
│       ├── case_cpu_burn.cpp
│       ├── case_cache_miss.cpp
│       ├── case_alloc_stress.cpp
│       └── ...                 # 新增 .cpp 自动编入
│
└── docs/                       # 中文详细文档
    ├── 01_内存泄漏.md
    ├── 02_卡顿优化.md
    ├── 03_帧率优化.md
    ├── 04_性能工具指南.md
    └── 05_ImGui性能测试平台.md
```

## 构建方法

### 前置要求
- CMake 3.16+
- Visual Studio 2019/2022 (MSVC) 或 MinGW
- Windows 10/11

### 构建步骤

```powershell
# 创建构建目录
mkdir build
cd build

# 生成项目 (Visual Studio)
cmake .. -G "Visual Studio 17 2022"

# 或使用 Ninja (更快)
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 构建所有目标
cmake --build . --config RelWithDebInfo

# 构建单个目标
cmake --build . --config RelWithDebInfo --target PerfCaseGUI
cmake --build . --config RelWithDebInfo --target stutter_cache_miss
```

### 运行

```powershell
# 运行 ImGui 性能测试平台
.\PerfCaseGUI\RelWithDebInfo\PerfCaseGUI.exe

# 运行命令行示例
.\memory_leak\RelWithDebInfo\memory_leak_basic.exe
.\stutter\RelWithDebInfo\stutter_cache_miss.exe
```

## 各模块学习指南

### memory_leak - 内存泄漏 (建议学习顺序)

| 序号 | 文件 | 核心知识点 |
|------|------|-----------|
| 1 | `01_basic_leak.cpp` | new/delete 遗漏、异常路径泄漏、容器裸指针 |
| 2 | `02_smart_ptr_fix.cpp` | unique_ptr、shared_ptr、性能对比 |
| 3 | `03_circular_reference.cpp` | 循环引用、weak_ptr、Observer 模式 |
| 4 | `04_win_api_leak.cpp` | HANDLE 泄漏、RAII 包装器、custom deleter |

### stutter - 卡顿优化

| 序号 | 文件 | 核心知识点 |
|------|------|-----------|
| 1 | `01_cache_miss.cpp` | 顺序/随机访问、AoS vs SoA、矩阵遍历方向 |
| 2 | `02_false_sharing.cpp` | 伪共享、alignas(64)、多线程计数器 |
| 3 | `03_lock_contention.cpp` | 粗/细粒度锁、shared_mutex、线程局部累加 |
| 4 | `04_blocking_io.cpp` | 同步/异步I/O、批量写入、内存映射文件 |

### framerate - 帧率优化

| 序号 | 文件 | 核心知识点 |
|------|------|-----------|
| 1 | `01_game_loop.cpp` | 固定/可变时间步长、插值渲染、帧率限制 |
| 2 | `02_frame_pacing.cpp` | P99 帧时间、帧步调分析、Sleep 精度 |
| 3 | `03_object_pooling.cpp` | 对象池 pattern、内存碎片、粒子系统 |

### win_perf - Windows 性能工具

| 序号 | 文件 | 核心知识点 |
|------|------|-----------|
| 1 | `01_etw_tracing.cpp` | ETW 事件追踪、WPR/WPA 使用指南 |
| 2 | `02_query_perf_counter.cpp` | QPC 高精度计时、benchmark 方法论 |

## 推荐的 Windows 性能工具

> 📖 详细介绍见 [docs/04_性能工具指南.md](docs/04_性能工具指南.md)

### 实时分析器

| 工具 | 用途 | 获取方式 |
|------|------|---------|
| **Tracy** | 实时帧/CPU/GPU/内存分析 | GitHub 开源 (BSD) |
| **Optick** | 轻量级游戏 Profiler | GitHub 开源 (MIT) |
| **Superluminal** | 商业级低开销采样分析 | 商业 ($299/年) |

### 硬件级分析器

| 工具 | 用途 | 获取方式 |
|------|------|---------|
| **Intel VTune** | CPU 硬件计数器深度分析 | 免费 (oneAPI) |
| **AMD μProf** | AMD CPU 硬件级分析 | 免费 |

### 系统级工具

| 工具 | 用途 | 获取方式 |
|------|------|---------|
| **WPR / WPA** | ETW 系统级追踪与分析 | Windows SDK |
| **Visual Studio Profiler** | CPU/内存分析 | Visual Studio |
| **PerfView** | .NET/Native 性能分析 | GitHub 开源 |
| **Process Monitor** | 文件/注册表 I/O 监控 | Sysinternals |
| **Application Verifier** | 堆损坏/句柄泄漏检测 | Windows SDK |

### 帧率分析

| 工具 | 用途 | 获取方式 |
|------|------|---------|
| **PresentMon** | 系统级帧时间测量 | GitHub 开源 |
| **FrameView** | 帧率统计报告 | NVIDIA 免费 |

### 内存分析

| 工具 | 用途 | 获取方式 |
|------|------|---------|
| **RAMMap / VMMap** | 物理/虚拟内存分析 | Sysinternals |
| **Dr. Memory** | 内存泄漏/越界检测 | 开源 |

### GPU 分析

| 工具 | 用途 | 获取方式 |
|------|------|---------|
| **NVIDIA Nsight Graphics** | GPU 帧调试与性能分析 | NVIDIA 免费 |
| **PIX for Windows** | DirectX 12 调试与分析 | 微软免费 |
| **RenderDoc** | GPU 帧捕获与调试 | 开源 (MIT) |
| **AMD RGP** | AMD GPU 性能分析 | AMD 免费 |

## 调试提示

- **Release 模式**测性能 (Debug 模式下结果不准确)
- **RelWithDebInfo** 是最佳选择 (优化 + 调试符号)
- 使用 `_CrtDumpMemoryLeaks()` 检测 CRT 内存泄漏
- 使用 Visual Studio 的 **Diagnostic Tools** 窗口实时监控

## 详细文档

`docs/` 目录下有每个模块的中文详细文档：

- [内存泄漏](docs/01_内存泄漏.md) — 泄漏模式、智能指针、循环引用、Windows API 资源管理
- [卡顿优化](docs/02_卡顿优化.md) — Cache miss、伪共享、锁竞争、阻塞 I/O
- [帧率优化](docs/03_帧率优化.md) — 游戏循环策略、帧步调分析、对象池
- [性能工具指南](docs/04_性能工具指南.md) — Tracy、VTune、WPA、PresentMon 等工具的详细介绍和使用方法
- [ImGui 性能测试平台](docs/05_ImGui性能测试平台.md) — PerfCase 框架、ADD_PERF_CASE 宏、添加用例教程、外部工具配合

---

## 免责声明

本项目是一个**纯学习用途的教学项目**，旨在帮助开发者了解和掌握常见的 C++ 性能优化技术。需要注意：

- **实际工程远比本项目复杂**: 真实世界的性能问题往往涉及多模块交互、并发架构、操作系统调度、硬件差异等因素，无法用简单示例完全复现
- **示例为了突出教学目的做了简化**: 省略了错误处理、日志、配置管理等生产代码必备的部分
- **性能数据仅供参考**: 测量结果受硬件、系统负载、编译器版本等因素影响，不同环境下可能有显著差异
- **请勿直接用于生产环境**: 本项目的代码未经过充分的健壮性和安全性审查
