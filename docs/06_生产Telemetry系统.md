# 生产 Telemetry 系统

## 概述

Telemetry 是一套面向**生产环境**的轻量级性能采集系统，用于在实际运行时（黑盒测试、线上版本）自动记录性能数据，无需任何人工干预。

与 Tracy Profiler 的定位对比：

| 特性 | Tracy | Telemetry |
|------|-------|-----------|
| 定位 | 开发调试 | 生产采集 |
| 查看方式 | 实时连接 Tracy GUI | 离线查看 JSON 文件 |
| 数据格式 | 专有协议 | Chrome Trace Event JSON |
| 可视化工具 | Tracy Profiler | Perfetto UI / chrome://tracing |
| 需要额外进程 | 是 (Tracy Server) | 否 |
| 适合场景 | 开发机调试、复现问题 | QA 测试、线上版本、自动化 |

**核心能力：**
- 多线程 Zone 耗时记录
- 锁竞争可视化（等待时间 / 持有时间）
- 自动卡顿检测 + 上下文捕获（卡顿时知道在干什么）
- 滚动自动导出（定时写 JSON，旧文件自动清理）
- 上传预算控制（防止数据量无限增长）
- 统一宏接口（`PERF_*` 宏，一套代码同时支持 Tracy 和 Telemetry）

---

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                    运行时 (游戏进程内)                        │
│                                                             │
│  Main Thread          IO Thread           Render Thread     │
│  ┌──────────┐         ┌──────────┐        ┌──────────┐     │
│  │ ZoneBegin │         │ ZoneBegin │        │ ZoneBegin │     │
│  │ ZoneEnd   │         │ LockWait  │        │ ZoneEnd   │     │
│  │ FrameMark │         │ LockAcq   │        │ LockWait  │     │
│  │ Context   │         │ ZoneEnd   │        │ LockAcq   │     │
│  └─────┬─────┘         └─────┬─────┘        └─────┬─────┘     │
│        │                     │                     │           │
│        ▼                     ▼                     ▼           │
│  ┌──────────┐         ┌──────────┐        ┌──────────┐       │
│  │ RingBuffer│         │ RingBuffer│        │ RingBuffer│       │
│  │  (64K)   │         │  (64K)   │        │  (64K)   │       │
│  └─────┬─────┘         └─────┬─────┘        └─────┬─────┘       │
│        │ SPSC 无锁          │                     │           │
│        └─────────┬───────────┘─────────────────────┘           │
│                  ▼                                             │
│         ┌────────────────┐                                    │
│         │  Drain Thread  │  ← 低优先级后台线程, 500ms 一次      │
│         │  汇聚所有 Buffer │                                    │
│         └───────┬────────┘                                    │
│                 ▼                                             │
│         ┌────────────────┐                                    │
│         │ events_ vector │  ← 全局事件池 (最多 200 万条)        │
│         └───────┬────────┘                                    │
│                 │  每 N 秒 / 退出时                             │
│                 ▼                                             │
│         ┌────────────────┐                                    │
│         │ Chrome Trace   │  → trace_20260321_143052.json      │
│         │ JSON 导出       │                                    │
│         └───────┬────────┘                                    │
│                 │  uploadCallback()                            │
│                 ▼                                             │
│         ┌────────────────┐                                    │
│         │ HTTP 上传服务器  │  (用户自行实现)                     │
│         └────────────────┘                                    │
└─────────────────────────────────────────────────────────────┘
```

---

## 编译与启用

```bash
# Telemetry 模式 (生产)
cmake -B build -DPERF_ENABLE_TELEMETRY=ON ..
cmake --build build --config Release

# Tracy 模式 (开发)
cmake -B build -DPERF_ENABLE_TRACY=ON ..
cmake --build build --config Release

# 关闭所有 (零开销, 默认)
cmake -B build ..
```

两者**互斥**，不可同时开启。编译期定义：

| CMake 选项 | 编译宏 | 效果 |
|---|---|---|
| `-DPERF_ENABLE_TRACY=ON` | `TRACY_ENABLE` | `PERF_*` → Tracy API |
| `-DPERF_ENABLE_TELEMETRY=ON` | `PERF_TELEMETRY_ENABLE` | `PERF_*` → telemetry:: API |
| 都不开 | 无 | `PERF_*` → `(void)0` |

---

## 统一宏接口

所有性能打点使用 `PERF_*` 宏，定义在 `common/tracy_integration.h`：

### 基础打点

```cpp
#include "tracy_integration.h"

void GameLoop() {
    Update();
    Render();
    PERF_FRAME_MARK;                    // 帧边界 (每帧末尾调用一次)
}

void Update() {
    PERF_ZONE_SCOPED;                   // 自动使用函数名 "Update"
    // ... 业务逻辑 ...
}

void LoadAsset(const char* path) {
    PERF_ZONE_SCOPED_N("LoadAsset");    // 自定义名称
    // ... 加载逻辑 ...
}
```

### 线程标记

```cpp
void IOThreadEntry() {
    PERF_SET_THREAD_NAME("IO Thread");  // 在 Perfetto 中按名称显示
    while (running) {
        PERF_ZONE_SCOPED_N("IO::Process");
        // ...
    }
}
```

### 锁竞争追踪

```cpp
// 声明可追踪的锁
PERF_LOCKABLE(std::mutex, sharedDataMutex);

void AccessSharedData() {
    std::lock_guard<decltype(sharedDataMutex)> lock(sharedDataMutex);
    // 临界区操作
}
```

Perfetto 中将显示：
- `Wait:sharedDataMutex` — 等待获取锁的时间（越宽 = 竞争越激烈）
- `Hold:sharedDataMutex` — 持有锁的时间

### 上下文标记（卡顿诊断）

```cpp
void LoadAsset(const char* path) {
    PERF_SCOPED_CONTEXT(path);          // 设置上下文，离开作用域自动清除
    PERF_ZONE_SCOPED_N("LoadAsset");
    // ... 加载逻辑 ...
}

void SwitchOutfit(const char* skinName) {
    PERF_SCOPED_CONTEXT(skinName);
    PERF_ZONE_SCOPED_N("SwitchOutfit");
    // ... 换装逻辑 ...
}
```

卡顿发生时，自动捕获所有线程的上下文。Perfetto 中的 STUTTER 标记会直接显示：
```
STUTTER 52.3ms | IO Thread: characters/hero_skin_03.fbx | Render Thread: DrawScene: particle_fire
```

不用点击就能知道卡顿时各线程在做什么。

### 消息与数值曲线

```cpp
PERF_MESSAGE_L("Scene loaded");         // 在时间线上标记事件
PERF_PLOT("FPS", currentFps);           // 数值曲线，Perfetto 中可视化
```

### 完整宏列表

| 宏 | 用途 | Tracy | Telemetry | OFF |
|---|---|---|---|---|
| `PERF_FRAME_MARK` | 帧边界标记 | `FrameMark` | `Manager::frameMark()` | `(void)0` |
| `PERF_ZONE_SCOPED` | 函数耗时 | `ZoneScoped` | `ZoneGuard(__FUNCTION__)` | `(void)0` |
| `PERF_ZONE_SCOPED_N(name)` | 自定义名称 Zone | `ZoneScopedN` | `ZoneGuard(name)` | `(void)0` |
| `PERF_ZONE_COLOR(color)` | 带颜色 Zone | `ZoneScopedC` | `ZoneGuard(fn, color)` | `(void)0` |
| `PERF_SET_THREAD_NAME(name)` | 线程名称 | `TracySetThreadName` | `SetThreadName()` | `(void)0` |
| `PERF_LOCKABLE(type, var)` | 可追踪锁 | `TracyLockable` | `TrackedMutex<type>` | `type var` |
| `PERF_SET_CONTEXT(text)` | 设置上下文 | `(void)0` | `SetContext()` | `(void)0` |
| `PERF_CLEAR_CONTEXT()` | 清除上下文 | `(void)0` | `ClearContext()` | `(void)0` |
| `PERF_SCOPED_CONTEXT(text)` | RAII 上下文 | `(void)0` | `ScopedContext` | `(void)0` |
| `PERF_MESSAGE_L(text)` | 字面量消息 | `TracyMessageL` | `RecordMessage()` | `(void)0` |
| `PERF_PLOT(name, val)` | 数值曲线 | `TracyPlot` | `RecordPlot()` | `(void)0` |
| `PERF_ALLOC(ptr, size)` | 内存分配 | `TracyAlloc` | *(TODO)* | `(void)0` |
| `PERF_FREE(ptr)` | 内存释放 | `TracyFree` | *(TODO)* | `(void)0` |

---

## Manager 生命周期

### 初始化

```cpp
#include "telemetry.h"

telemetry::Config cfg;
cfg.stutterThresholdMs    = 33.3f;       // 超过 33ms 视为卡顿
cfg.outputDir             = "./telemetry_output";
cfg.autoExport            = true;        // 退出时自动导出
cfg.autoExportIntervalSec = 60.0f;       // 每 60 秒滚动导出
cfg.maxTraceFiles         = 5;           // 最多保留 5 个文件
cfg.uploadBudgetBytes     = 20 * 1024 * 1024;  // 单次会话最多上传 20MB
cfg.uploadCallback = [](const std::string& path) {
    // 自定义上传逻辑
    httpUpload("https://your-server/api/traces", path);
};

telemetry::Manager::get().init(cfg);
```

### 运行时

```cpp
// 主循环
while (running) {
    PERF_ZONE_SCOPED;
    Update();
    Render();

    // 帧标记 + 自动卡顿检测
    PERF_FRAME_MARK;
}
```

### 关闭

```cpp
telemetry::Manager::get().shutdown();
// shutdown 内部:
//   1. 停止后台采集线程
//   2. 最终 drain 所有缓冲区
//   3. 如果 autoExport=true, 自动导出最后一段数据
//   4. 清理线程状态
```

---

## 滚动自动导出

设置 `autoExportIntervalSec > 0` 后，后台线程每隔 N 秒自动：

1. 将当前事件池导出为 JSON 文件（`trace_YYYYMMDD_HHMMSS.json`）
2. 清空事件池，开始采集下一段
3. 检查 `maxTraceFiles`，超过则删除最旧的文件
4. 如果配置了 `uploadCallback`，检查上传预算后调用

```
telemetry_output/
  trace_20260321_140000.json   ← 最旧，超过 maxTraceFiles 时自动删除
  trace_20260321_140100.json
  trace_20260321_140200.json
  trace_20260321_140300.json
  trace_20260321_140400.json   ← 最新
```

---

## 上传预算控制

`uploadBudgetBytes` 限制单次游戏会话的总上传量：

| 设置 | 行为 |
|------|------|
| `0` | 不限制（开发阶段适用） |
| `20 * 1024 * 1024` | 最多上传 20MB，超出后文件仍然本地保存但不调用 `uploadCallback` |

这确保：
- 黑盒测试人员正常游玩时不会产生无限网络流量
- 本地文件仍然保留，需要时可手动拉取
- 配合 `maxTraceFiles` 限制磁盘占用

### 生产环境推荐配置

```cpp
telemetry::Config cfg;
cfg.stutterThresholdMs    = 33.3f;       // 30 FPS 标准
cfg.autoExportIntervalSec = 60.0f;       // 每分钟导出
cfg.maxTraceFiles         = 5;           // 磁盘最多 5 个文件
cfg.uploadBudgetBytes     = 20ULL * 1024 * 1024;  // 最多 20MB 上传
cfg.drainIntervalMs       = 1000.0f;     // 采集频率降低，减少开销
```

---

## 查看 Trace 数据

### 方法一：Perfetto UI（推荐）

1. 浏览器打开 **https://ui.perfetto.dev**
2. 把 `telemetry_output/trace_xxx.json` 文件**拖入浏览器窗口**
3. 完成！

> Perfetto UI 是纯前端应用，**数据不会上传到 Google**，完全在本地浏览器中解析。

### 方法二：chrome://tracing

1. Chrome 浏览器地址栏输入 `chrome://tracing`
2. 点击 "Load" 按钮
3. 选择 `.json` 文件

### 如何阅读时间线

```
时间轴 (横轴 = 时间, 从左到右)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
▲ STUTTER 52.3ms | IO: hero_skin.fbx
          ↑ 卡顿标记 (三角形, 全局可见)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
IO Thread:
  ███ IO::LoadAsset ███   ██ Hold:SharedData ██
                          ██ IO::UpdateCache ██
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Render Thread:
                               ██ Render::DrawScene ██
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Main Thread:
  ██ DrawUI ██
    ██ PerfCase::onUpdate ██
      ██ Main::GameLogic ██
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

**各元素含义：**

| 元素 | 含义 |
|------|------|
| 色块宽度 | 耗时长度（越宽 = 越久） |
| 色块嵌套 | 父子函数调用关系 |
| `Wait:xxx` | 等待获取锁的时间 |
| `Hold:xxx` | 持有锁的时间 |
| `▲ STUTTER` | 卡顿标记（全局可见, 直接显示上下文） |
| `Frame` 标记 | 帧边界 |

**操作快捷键 (Perfetto)：**

| 按键 | 操作 |
|------|------|
| `W` / `S` | 放大 / 缩小时间轴 |
| `A` / `D` | 左移 / 右移 |
| `F` | 聚焦选中事件 |
| `/` | 搜索事件名（如搜 "STUTTER"） |
| 单击色块 | 查看详细 args（含 context） |
| 框选区域 | 底部显示选区内所有事件的统计汇总 |

---

## 性能开销

| 操作 | 耗时 | 说明 |
|------|------|------|
| `PERF_ZONE_SCOPED` | ~25ns | QPC 读取 + SPSC push（1 个原子 store） |
| `PERF_FRAME_MARK` | ~30ns | 同上 + 帧时间比较 |
| `PERF_SCOPED_CONTEXT` | ~5ns | 仅设置一个指针 |
| `Lock/Unlock` | ~50ns | 两次事件记录 |
| 后台 Drain | ~200μs/次 | 低优先级线程, 500ms 间隔 |

### 为什么这么快

1. **Per-thread SPSC RingBuffer** — 每个线程写自己的缓冲区，热路径只有一个 `atomic store`，没有锁、没有 CAS 竞争
2. **`const char*` 指针** — `name` 指向字面量，不复制字符串
3. **`Event` 仅 32 字节** — 一次写入在一个缓存行内完成
4. **`head_` 和 `tail_` 分别 64 字节对齐** — 避免 CPU 缓存行伪共享

---

## 核心原理

### SPSC 无锁 Ring Buffer

```
┌─────────────────────────────────────────┐
│  events_[65536]                         │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┐     │
│  │   │   │ E │ E │ E │ E │   │   │     │
│  └───┴───┴─▲─┴───┴───┴─▲─┴───┴───┘     │
│            │            │               │
│          tail_        head_             │
│       (consumer)   (producer)           │
│                                         │
│  Producer (业务线程): push() → head_++  │
│  Consumer (drain线程): pop()  → tail_++ │
│                                         │
│  只有一个 writer, 一个 reader            │
│  无需 CAS, 只需 acquire/release 语义     │
└─────────────────────────────────────────┘
```

### 卡顿检测

```
frameMark() 每帧调用:
  ① now = QueryPerformanceCounter()
  ② frameMs = (now - lastFrameTs) / 1000
  ③ if frameMs > threshold:
       context = 遍历所有线程的 context 指针, 拼接字符串
       保存 StutterInfo { timestamp, frameMs, context }
  ④ lastFrameTs = now
```

`context` 是 `const char*`，卡顿检测线程只做指针读取，不需要原子操作。

### 锁竞争可视化

```
TrackedMutex::lock():
  ① RecordLockEvent(LockWait)   → 开始 "Wait:SharedData"
  ② mtx_.lock()                  → 实际等待
  ③ RecordLockEvent(LockAcquire) → 结束 "Wait", 开始 "Hold"

TrackedMutex::unlock():
  ④ RecordLockEvent(LockRelease) → 结束 "Hold"
```

导出时 `LockAcquire` 生成两个 Chrome Trace 事件：

```json
{"ph":"E","name":"Wait:SharedData",...}    // 结束等待
{"ph":"B","name":"Hold:SharedData",...}    // 开始持有
```

Perfetto 中表现为两个紧邻的色块，等待越久 Wait 越宽。

### Chrome Trace Event JSON 格式

输出遵循 [Trace Event Format](https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview) 规范：

```json
{"traceEvents":[
  {"ph":"M","name":"thread_name","pid":15596,"tid":17164,
   "args":{"name":"IO Thread"}},

  {"ph":"B","name":"IO::LoadAsset","cat":"zone",
   "pid":15596,"tid":17164,"ts":334000000},
  {"ph":"E","cat":"zone",
   "pid":15596,"tid":17164,"ts":337623000},

  {"ph":"i","name":"STUTTER 52.3ms | IO: hero_skin.fbx",
   "cat":"stutter","pid":15596,"tid":83648,
   "ts":335000000,"s":"g",
   "args":{"frame_ms":52.3,"frame_index":42,
           "context":"IO: hero_skin.fbx"}}
]}
```

| 字段 | 含义 |
|------|------|
| `ph:"B"` / `ph:"E"` | Begin / End → Perfetto 画成色块 |
| `ph:"i"` | Instant → Perfetto 画成三角标记 |
| `ph:"M"` | Metadata → 给线程 ID 取名 |
| `ph:"C"` | Counter → 数值曲线 |
| `tid` | 线程 ID → Perfetto 按此分行 |
| `ts` | 微秒时间戳 → 横轴位置 |
| `s:"g"` | 全局可见（所有线程泳道都显示） |
| `cat` | 类别（zone / lock / stutter / msg） |

---

## 文件结构

```
common/
  telemetry.h              # 核心头文件: Event, RingBuffer, ZoneGuard,
                           #   TrackedMutex, ScopedContext, Manager, Config
  telemetry.cpp            # 实现: drain, export, 卡顿检测, 文件轮转
  tracy_integration.h      # PERF_* 统一宏 (三路路由)

PerfCaseGUI/cases/
  case_telemetry_demo.cpp  # 演示用例: 3 线程 + 锁竞争 + 上下文
```

---

## 集成到引擎

### 第一步：初始化

在引擎启动时：

```cpp
#ifdef PERF_TELEMETRY_ENABLE
telemetry::Config cfg;
cfg.stutterThresholdMs    = 33.3f;
cfg.autoExportIntervalSec = 60.0f;
cfg.maxTraceFiles         = 5;
cfg.uploadBudgetBytes     = 20ULL * 1024 * 1024;
cfg.uploadCallback = [](const std::string& path) {
    // 投递到上传队列
    g_uploadQueue.push(path);
};
telemetry::Manager::get().init(cfg);
PERF_SET_THREAD_NAME("Main Thread");
#endif
```

### 第二步：埋点

```cpp
// 函数级
void PhysicsSystem::Update() {
    PERF_ZONE_SCOPED;
    // ...
}

// 带资源上下文
void AssetManager::Load(const char* path) {
    PERF_SCOPED_CONTEXT(path);        // 卡顿时能看到资源路径
    PERF_ZONE_SCOPED_N("AssetLoad");
    // ...
}

// 锁
class RenderQueue {
    PERF_LOCKABLE(std::mutex, queueMutex_);
    void Submit() {
        std::lock_guard<decltype(queueMutex_)> lock(queueMutex_);
        // ...
    }
};

// 帧标记
void Engine::RunFrame() {
    Update();
    Render();
    PERF_FRAME_MARK;
}
```

### 第三步：各线程入口

```cpp
void RenderThreadEntry() {
    PERF_SET_THREAD_NAME("Render Thread");
    while (running) {
        PERF_ZONE_SCOPED_N("Render::Frame");
        // ...
    }
}

void IOThreadEntry() {
    PERF_SET_THREAD_NAME("IO Thread");
    // ...
}
```

### 第四步：关闭

```cpp
#ifdef PERF_TELEMETRY_ENABLE
telemetry::Manager::get().shutdown();
#endif
```

### 编译控制

- 开发版：`-DPERF_ENABLE_TRACY=ON`（连接 Tracy 实时调试）
- 内测版：`-DPERF_ENABLE_TELEMETRY=ON`（自动采集上传）
- 正式版：都不开（零开销）

---

## FAQ

### Q: 不需要自建 Perfetto 吗？

不需要。https://ui.perfetto.dev 是 Google 官方工具，免费、纯前端、数据不上传。直接把 JSON 拖进去就能看。

### Q: 数据量会不会太大？

通过三层控制：
1. `maxEvents = 2000000` — 单文件最多 200 万事件，约 50-100MB JSON
2. `maxTraceFiles = 5` — 磁盘最多保留 5 个文件
3. `uploadBudgetBytes` — 限制单次会话上传总量

### Q: name 参数为什么必须是字面量？

`Event.name` 是 `const char*` 指针，指向源码中的字符串字面量。这样做的好处：
- 零拷贝，写入只需赋值一个指针
- 字面量生命周期 = 程序生命周期，不会悬空

**错误用法：**
```cpp
std::string s = "dynamic_" + std::to_string(id);
PERF_ZONE_SCOPED_N(s.c_str());  // ❌ s 析构后指针悬空
```

**正确用法：**
```cpp
PERF_ZONE_SCOPED_N("LoadAsset");           // ✅ 字面量
PERF_SCOPED_CONTEXT("hero_skin_03.fbx");   // ✅ 字面量
```

如果需要动态名称，使用 `PERF_MESSAGE_L()` 记录消息。

### Q: RingBuffer 满了怎么办？

丢弃新事件并计数（`dropped_` 原子自增）。UI 中可查看 `Dropped Events` 数量。
64K 容量 × 500ms drain 间隔 = 每秒最多 128K 事件，正常游戏一帧几百个 Zone 完全够用。

### Q: 可以同时启用 Tracy 和 Telemetry 吗？

不可以。CMake 中 `PERF_ENABLE_TRACY` 和 `PERF_ENABLE_TELEMETRY` 互斥。
一套 `PERF_*` 宏只能路由到一个后端。
