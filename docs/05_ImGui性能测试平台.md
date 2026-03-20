# ImGui 性能测试平台

## 概述

PerfTest 项目提供了一个基于 **ImGui + DirectX 11** 的窗口化性能测试平台。
所有性能用例通过 `ADD_PERF_CASE` 宏自动注册，链接后即可在界面中选择和执行。

该平台的核心优势:
- **持续运行**: 应用窗口常驻，可用 WPR/WPA、Tracy、VTune 等外部工具持续采集数据
- **零配置注册**: 新用例只需实现一个类 + 一行宏，编译后自动出现在界面中
- **实时观测**: 帧时间图、P99/Max 统计、每个用例的单独耗时

---

## 架构

```
common/
  perf_case.h          # PerfCase 基类 + PerfRegistry + ADD_PERF_CASE 宏

PerfCaseGUI/
  CMakeLists.txt         # 自动收集 cases/*.cpp
  main.cpp               # DX11 + ImGui 主循环，驱动注册表
  cases/
    case_cpu_burn.cpp       # CPU 密集计算
    case_thread_contention.cpp  # 多线程竞争
    case_cache_miss.cpp     # 缓存未命中
    case_false_sharing.cpp  # 伪共享
    case_lock_contention.cpp # 锁竞争策略
    case_blocking_io.cpp    # 阻塞 I/O
    case_alloc_stress.cpp   # 内存分配压力
    case_memory_leak.cpp    # 内存泄漏模拟
    case_stutter_sim.cpp    # 卡顿注入
    case_particle_system.cpp # 粒子系统
```

---

## PerfCase 框架

### 基类接口

```cpp
class PerfCase {
public:
    // --- 必须实现 ---
    virtual const char* getCategory()    const = 0;  // 分类: "CPU", "Memory", "Stutter", "Framerate"
    virtual const char* getName()        const = 0;  // 用例名称
    virtual const char* getDescription() const = 0;  // 简短描述

    // --- 生命周期 (可选覆盖) ---
    virtual void onActivate()   {}      // 被选中激活时调用
    virtual void onDeactivate() {}      // 被取消激活时调用

    // --- 每帧调用 (激活状态下) ---
    virtual void onUpdate(float deltaTime) {}  // 每帧的工作负载

    // --- ImGui 控制面板 ---
    virtual void onDrawUI() = 0;        // 绘制参数控制和结果显示

    // --- 框架管理的状态 ---
    bool  active       = false;
    float lastUpdateMs = 0.0f;  // 上一次 onUpdate 的耗时

    // --- 工具方法 ---
    template<typename Fn>
    float measure(Fn&& fn);   // 返回代码块执行耗时 (ms)
};
```

### 注册宏

```cpp
ADD_PERF_CASE(ClassName)
```

在 `.cpp` 文件末尾调用此宏，即可将用例自动注册到全局注册表。
原理: 宏在匿名命名空间中声明一个静态对象，其构造函数在程序启动时将用例实例添加到 `PerfRegistry`。

---

## 如何添加新用例

### 步骤

1. 在 `PerfCaseGUI/cases/` 下创建新的 `.cpp` 文件
2. 定义一个继承 `PerfCase` 的类，实现必要的虚函数
3. 文件末尾用 `ADD_PERF_CASE(ClassName)` 注册
4. **无需手动修改 CMakeLists.txt** — `file(GLOB CONFIGURE_DEPENDS)` 会自动收集新文件

### 完整示例

假设要添加一个 **排序算法对比** 的测试用例:

```cpp
// PerfCaseGUI/cases/case_sort_benchmark.cpp

#include "perf_case.h"
#include "imgui.h"
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>

class SortBenchmarkCase : public PerfCase {
public:
    const char* getCategory()    const override { return "CPU"; }
    const char* getName()        const override { return "Sort Benchmark"; }
    const char* getDescription() const override {
        return "Compare std::sort vs std::stable_sort performance.\n"
               "Click 'Run Benchmark' to measure.";
    }

    void onDrawUI() override {
        ImGui::SliderInt("Array Size", &arraySize_, 1000, 5000000, "%d",
                         ImGuiSliderFlags_Logarithmic);

        if (ImGui::Button("Run Benchmark")) {
            runBenchmark();
        }

        if (sortMs_ > 0) {
            ImGui::Text("std::sort:        %.2f ms", sortMs_);
            ImGui::Text("std::stable_sort: %.2f ms", stableSortMs_);

            float ratio = stableSortMs_ / sortMs_;
            ImGui::Text("Ratio (stable/sort): %.2fx", ratio);
        }
    }

private:
    int   arraySize_    = 100000;
    float sortMs_       = 0;
    float stableSortMs_ = 0;

    void runBenchmark() {
        std::vector<int> data(arraySize_);
        std::iota(data.begin(), data.end(), 0);
        std::mt19937 rng(42);

        auto data2 = data;
        std::shuffle(data.begin(), data.end(), rng);
        data2 = data;  // same initial state

        sortMs_ = measure([&]() {
            std::sort(data.begin(), data.end());
        });

        stableSortMs_ = measure([&]() {
            std::stable_sort(data2.begin(), data2.end());
        });
    }
};

ADD_PERF_CASE(SortBenchmarkCase)
```

然后重新编译即可，新用例会自动出现在界面左侧面板的 "CPU" 分类下。

> **注意**: `PerfCaseGUI/CMakeLists.txt` 使用 `file(GLOB CONFIGURE_DEPENDS cases/*.cpp)` 自动收集，
> 新增 `.cpp` 文件后无需手动修改 CMakeLists.txt，重新构建即可生效。

---

## 界面使用指南

### 布局

| 区域 | 内容 |
|------|------|
| 左侧面板 "Perf Cases" | 所有用例按分类分组显示，帧率统计和帧时间图在顶部 |
| 右侧面板 "Case Detail" | 选中用例的描述、激活开关和自定义控制面板 |

### 操作

| 操作 | 说明 |
|------|------|
| **左键单击** 用例 | 选中并在右侧查看详情 |
| **右键单击** 用例 | 弹出菜单，快速 Activate / Deactivate |
| **Active 复选框** | 在右侧面板中开关用例 |
| **Deactivate All** | 一键关闭所有激活的用例 |

### 用例类型

**持续模式 (Continuous)**: `onUpdate()` 每帧执行，用于模拟实时负载
- CPU Burn, Thread Contention, Alloc Stress, Memory Leak, Stutter Sim, Particle System

**基准测试 (Benchmark)**: 点击按钮触发一次性测量，结果显示在界面中
- Cache Miss, False Sharing, Lock Contention, Blocking I/O

**混合模式**: Benchmark + 可选每帧负载
- Alloc Stress, Cache Miss

---

## 与外部性能工具配合使用

该平台的设计初衷是方便配合专业性能工具来定位问题。以下是典型工作流:

### WPR / WPA (Windows Performance Recorder / Analyzer)

1. 启动 PerfTest 应用，激活待分析的用例
2. 运行 WPR 开始录制:
   ```powershell
   wpr -start CPU
   ```
3. 在应用中运行一段时间，产生足够数据
4. 停止录制并分析:
   ```powershell
   wpr -stop output.etl
   wpa output.etl
   ```
5. 在 WPA 中查看 CPU Usage (Sampled) 图表，定位热点函数

### Tracy Profiler

1. 编译时启用 Tracy:
   ```powershell
   cmake -DPERF_ENABLE_TRACY=ON ..
   cmake --build . --config RelWithDebInfo --target PerfCaseGUI
   ```
2. 项目已在关键路径自动插入 Tracy 标记:
   - `FrameMark` — 主循环帧边界
   - `ZoneScoped` — DrawUI、PerfCase::onUpdate 等函数
   - 各 PerfCase 用例的 onUpdate 也已标记
3. 启动 Tracy Profiler 连接到应用，查看实时帧图和 Zone 耗时
4. 如需在自定义用例中添加标记:
   ```cpp
   #include "tracy_integration.h"

   void onUpdate(float dt) override {
       PERF_ZONE_SCOPED;  // Tracy 标记
       // ... 工作负载 ...
   }
   ```

### Intel VTune Profiler

1. 启动 VTune，选择 Hotspots / Microarchitecture Exploration 等分析模式
2. 目标设为 `PerfCaseGUI.exe`
3. 开始分析，在应用中激活用例
4. 停止采集，查看热点函数和 CPU 微架构瓶颈

### Visual Studio 性能探查器

1. 在 VS 中打开解决方案 (`build/PerfTest.sln`)
2. Debug → Performance Profiler
3. 选择 CPU Usage 或 .NET Object Allocation 等工具
4. 关联到正在运行的 `PerfCaseGUI.exe` 进程
5. 采集一段时间后查看结果

---

## 已内置的性能用例

### CPU 类

| 用例 | 描述 |
|------|------|
| **CPU Burn** | 持续 sin/cos 密集运算，可调迭代次数 (100K ~ 50M) |
| **Thread Contention** | 多线程互斥锁竞争，可调线程数和操作数量 |

### Memory 类

| 用例 | 描述 |
|------|------|
| **Alloc Stress** | malloc/free vs Object Pool 对比，支持持续分配和基准测试 |
| **Memory Leak** | 故意泄漏模拟器，实时显示进程内存占用 (Working Set) |

### Stutter 类 (卡顿)

| 用例 | 描述 |
|------|------|
| **Cache Miss** | 顺序 vs 随机内存访问，可调数组大小，直方图可视化 |
| **False Sharing** | 紧密排列 vs 缓存行对齐的计数器，多线程竞争 |
| **Lock Contention** | 单 Mutex vs 分片锁 vs Atomic 对比，直方图可视化 |
| **Blocking I/O** | 不同文件读写策略的性能对比 (flush/buffered/ReadFile) |

### Framerate 类

| 用例 | 描述 |
|------|------|
| **Stutter Sim** | 随机注入指定时长的卡顿，可选 busy-wait 或 Sleep |
| **Particle System** | 大规模粒子渲染 (最高 1M)，ImGui Canvas 可视化 |

---

## 编译与运行

```powershell
# 配置 (首次)
cd build
cmake .. -G "Visual Studio 17 2022"

# 编译 (推荐 RelWithDebInfo，带调试符号)
cmake --build . --config RelWithDebInfo --target PerfCaseGUI

# 运行
.\PerfCaseGUI\RelWithDebInfo\PerfCaseGUI.exe
```

> **提示**: 使用 `RelWithDebInfo` 配置编译，既有优化性能又保留调试符号，最适合配合 VTune、WPA 等工具分析。

---

## 设计原则

- **低耦合**: 每个用例是一个独立的 `.cpp` 文件，不依赖其他用例
- **自动发现**: 用例通过静态初始化自动注册，无需手动维护列表
- **交互友好**: 每个用例自行绘制 ImGui 控件，自由控制参数和展示结果
- **工具友好**: 常驻窗口 + 可控负载 = 完美配合外部 profiler

---

## 常见问题

**Q: 新添加的用例编译后没有出现在界面中？**
A: 确认 `.cpp` 文件已放在 `PerfCaseGUI/cases/` 目录下。
   CMake 会自动收集该目录下的所有 `.cpp` 文件，重新构建即可。

**Q: 某些用例的 onUpdate 耗时很高导致整体卡顿？**
A: 这是预期行为。可以在界面中降低该用例的参数强度，或暂时 Deactivate。
   如果想用外部工具分析，建议一次只激活一个用例以便隔离问题。

**Q: 如何让用例在 onDrawUI 中绘制自定义图形？**
A: 可以使用 `ImGui::GetWindowDrawList()` 或创建子画布进行自定义绘制。
   参考 `case_particle_system.cpp` 中的粒子渲染实现。
