/*
 * telemetry.h - 轻量级生产环境性能采集系统
 *
 * 设计目标:
 *   - 极低开销: 每个 Zone ~25ns (QPC + lock-free 写入)
 *   - 多线程安全: Per-thread SPSC ring buffer, 热路径零竞争
 *   - 卡顿检测: 帧时间超阈值自动标记
 *   - 锁竞争监控: TrackedMutex 记录等待/持有时间
 *   - Chrome Trace 导出: 可在 chrome://tracing 或 Perfetto 中查看
 *   - 上传扩展: 预留回调接口, 可对接自有服务器
 *
 * 与 Tracy 的关系:
 *   Tracy 用于开发调试 (实时连接查看), 本系统用于生产采集 (自动记录上传)
 *   通过 CMake -DPERF_ENABLE_TELEMETRY=ON 启用宏路由
 *   PERF_* 宏自动路由到对应后端
 *   也可直接使用 telemetry:: API (不依赖宏)
 *
 * BSD 3-Clause License (同 Tracy)
 */

#pragma once

// ============================================================
// 平台要求:
//   - C++14 或更高 (MSVC 2017 v141 / v141_xp 均可)
//   - Windows: XP SP3+ (使用 QPC/Win32 API, 不依赖 std::filesystem)
//   - Linux/macOS: GCC 5+ / Clang 3.4+
//
// 集成方式:
//   方式一 (推荐): 把 telemetry.h / telemetry.cpp / tracy_integration.h
//                  直接加入你的引擎项目 (DLL 或 EXE) 编译
//   方式二: 编译为静态库 (.lib) 链接
//   方式三: 编译为独立 DLL (需定义 TELEMETRY_DLL_EXPORT)
//
// DLL 边界说明:
//   如果 telemetry 编译在 DLL 内, 在 DLL 工程中定义 TELEMETRY_DLL_EXPORT,
//   使用方 #include 时自动变为 dllimport.
//   如果直接源码编入同一个模块, 不需要定义任何宏.
// ============================================================

// --- DLL 导出宏 ---
#if defined(TELEMETRY_DLL_EXPORT)
    #define TELEMETRY_API __declspec(dllexport)
#elif defined(TELEMETRY_DLL_IMPORT)
    #define TELEMETRY_API __declspec(dllimport)
#else
    #define TELEMETRY_API
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

namespace telemetry {

// ============================================================
// 高精度时间戳 (微秒, Windows 使用 QPC)
// ============================================================
inline int64_t Now() {
#ifdef _WIN32
    static const int64_t freq = []() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // 防溢出: 拆分为秒和余数
    int64_t seconds = now.QuadPart / freq;
    int64_t remainder = now.QuadPart % freq;
    return seconds * 1000000 + (remainder * 1000000) / freq;
#else
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
#endif
}

inline uint32_t CurrentThreadId() {
#ifdef _WIN32
    return ::GetCurrentThreadId();
#else
    return static_cast<uint32_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

// ============================================================
// 事件类型
// ============================================================
enum class EventType : uint8_t {
    ZoneBegin,
    ZoneEnd,
    FrameMark,
    LockWait,
    LockAcquire,
    LockRelease,
    Message,
    ThreadName,
    Plot
};

// ============================================================
// 事件结构体
// name 指针必须指向静态生命周期字符串 (字面量/全局)
// ============================================================
struct Event {
    EventType   type;
    uint32_t    threadId;
    int64_t     timestamp;   // 微秒
    const char* name;
    union {
        uint32_t color;
        uint32_t lockId;
        double   plotVal;
    };
};

// ============================================================
// SPSC 无锁环形缓冲区 (每线程一个)
//
// Producer = 拥有线程 (push), Consumer = 后台采集线程 (pop)
// 容量 64K 事件, 溢出时丢弃新事件并计数
// ============================================================
class RingBuffer {
public:
    static constexpr uint32_t CAPACITY = 65536;
    static constexpr uint32_t MASK = CAPACITY - 1;

    bool push(const Event& e) {
        uint32_t h = head_.load(std::memory_order_relaxed);
        uint32_t t = tail_.load(std::memory_order_acquire);
        if (h - t >= CAPACITY) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        events_[h & MASK] = e;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    bool pop(Event& e) {
        uint32_t t = tail_.load(std::memory_order_relaxed);
        uint32_t h = head_.load(std::memory_order_acquire);
        if (t == h) return false;
        e = events_[t & MASK];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    uint32_t count() const {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_acquire);
    }

    uint64_t dropped() const {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    Event events_[CAPACITY];
    alignas(64) std::atomic<uint32_t> head_{0};
    alignas(64) std::atomic<uint32_t> tail_{0};
    std::atomic<uint64_t> dropped_{0};
};

// ============================================================
// 线程状态 (每线程一个, 堆分配, Manager 持有指针)
// ============================================================
struct ThreadState {
    RingBuffer buffer;
    uint32_t   threadId = 0;
    char       name[64] = {};
    bool       named = false;
    bool       registered = false;
    const char* context = nullptr;  // 当前操作上下文 (用于卡顿诊断)
};

// 获取当前线程的 ThreadState (首次调用自动创建, Manager 激活后自动注册)
TELEMETRY_API ThreadState& GetThreadState();

// ============================================================
// 事件记录 (inline, 热路径, ~25ns per call)
// ============================================================
inline void RecordZoneBegin(const char* name, uint32_t color = 0) {
    auto& ts = GetThreadState();
    Event e{};
    e.type = EventType::ZoneBegin;
    e.threadId = ts.threadId;
    e.timestamp = Now();
    e.name = name;
    e.color = color;
    ts.buffer.push(e);
}

inline void RecordZoneEnd() {
    auto& ts = GetThreadState();
    Event e{};
    e.type = EventType::ZoneEnd;
    e.threadId = ts.threadId;
    e.timestamp = Now();
    e.name = nullptr;
    e.color = 0;
    ts.buffer.push(e);
}

inline void RecordFrameMark(const char* name = nullptr) {
    auto& ts = GetThreadState();
    Event e{};
    e.type = EventType::FrameMark;
    e.threadId = ts.threadId;
    e.timestamp = Now();
    e.name = name;
    e.color = 0;
    ts.buffer.push(e);
}

inline void RecordLockEvent(EventType type, uint32_t lockId, const char* name) {
    auto& ts = GetThreadState();
    Event e{};
    e.type = type;
    e.threadId = ts.threadId;
    e.timestamp = Now();
    e.name = name;
    e.lockId = lockId;
    ts.buffer.push(e);
}

inline void RecordMessage(const char* text) {
    auto& ts = GetThreadState();
    Event e{};
    e.type = EventType::Message;
    e.threadId = ts.threadId;
    e.timestamp = Now();
    e.name = text;
    e.color = 0;
    ts.buffer.push(e);
}

inline void RecordPlot(const char* name, double val) {
    auto& ts = GetThreadState();
    Event e{};
    e.type = EventType::Plot;
    e.threadId = ts.threadId;
    e.timestamp = Now();
    e.name = name;
    e.plotVal = val;
    ts.buffer.push(e);
}

// 设置当前线程名称 (非 inline, 定义在 telemetry.cpp)
TELEMETRY_API void SetThreadName(const char* name);

// ============================================================
// 上下文标记 (用于卡顿诊断)
//
// 在耗时操作前设置上下文, 卡顿检测时自动捕获各线程上下文
// 用法:
//   telemetry::SetContext("Loading: hero_skin.fbx");
//   loadAsset(path);
//   telemetry::ClearContext();
//
// 或 RAII 方式:
//   telemetry::ScopedContext ctx("Loading: hero_skin.fbx");
// ============================================================
inline void SetContext(const char* ctx) {
    GetThreadState().context = ctx;
}

inline void ClearContext() {
    GetThreadState().context = nullptr;
}

class ScopedContext {
public:
    explicit ScopedContext(const char* ctx) { SetContext(ctx); }
    ~ScopedContext() { ClearContext(); }
    ScopedContext(const ScopedContext&) = delete;
    ScopedContext& operator=(const ScopedContext&) = delete;
};

// ============================================================
// RAII Zone Guard
// ============================================================
class ZoneGuard {
public:
    explicit ZoneGuard(const char* name, uint32_t color = 0) {
        RecordZoneBegin(name, color);
    }
    ~ZoneGuard() {
        RecordZoneEnd();
    }
    ZoneGuard(const ZoneGuard&) = delete;
    ZoneGuard& operator=(const ZoneGuard&) = delete;
};

// ============================================================
// 锁竞争监控 Mutex 包装器
//
// 用法:
//   telemetry::TrackedMutex<std::mutex> myMutex("SharedData");
//   { std::lock_guard<decltype(myMutex)> lock(myMutex); ... }
//
// Chrome Trace 中可视化:
//   "Wait:SharedData"  → 线程等待获取锁的时间
//   "Hold:SharedData"  → 线程持有锁的时间
// ============================================================
template<typename Mutex = std::mutex>
class TrackedMutex {
public:
    explicit TrackedMutex(const char* name)
        : name_(name), id_(nextId()) {}

    void lock() {
        RecordLockEvent(EventType::LockWait, id_, name_);
        mtx_.lock();
        RecordLockEvent(EventType::LockAcquire, id_, name_);
    }

    void unlock() {
        RecordLockEvent(EventType::LockRelease, id_, name_);
        mtx_.unlock();
    }

    bool try_lock() {
        if (mtx_.try_lock()) {
            RecordLockEvent(EventType::LockAcquire, id_, name_);
            return true;
        }
        return false;
    }

    uint32_t id() const { return id_; }
    const char* name() const { return name_; }

private:
    Mutex mtx_;
    const char* name_;
    uint32_t id_;

    static uint32_t nextId() {
        static std::atomic<uint32_t> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }
};

// ============================================================
// 卡顿信息
// ============================================================
struct StutterInfo {
    int64_t     timestamp;      // 发生时间 (微秒)
    float       frameTimeMs;    // 帧耗时 (毫秒)
    uint32_t    threadId;
    uint64_t    frameIndex;     // 帧序号
    std::string context;        // 卡顿时各线程的操作上下文
};

// ============================================================
// 配置
//
// 可通过代码构造, 也可通过 telemetry.ini 文件加载:
//   Config cfg = Config::loadFromFile("telemetry.ini");
//   Manager::get().init(cfg);
// 或直接:
//   Manager::get().init("telemetry.ini");
// 或无参调用 (自动查找 exe 同目录 / 当前目录下的 telemetry.ini):
//   Manager::get().init();
// ============================================================
struct Config {
    float       stutterThresholdMs = 33.3f;    // 帧时间超过此值视为卡顿
    float       drainIntervalMs    = 500.0f;   // 后台采集间隔
    std::string outputDir          = "./telemetry_output";
    bool        autoExport         = true;     // shutdown 时自动导出
    uint32_t    maxEvents          = 2000000;  // 单文件最大事件数

    // --- 仅卡顿模式 (只保存卡顿帧附近的数据, 大幅减少数据量) ---
    bool        stutterOnly           = false;
    float       stutterCaptureBeforeMs = 100.0f;
    float       stutterCaptureAfterMs  = 50.0f;

    // --- 滚动导出 (运行期间自动导出) ---
    float       autoExportIntervalSec = 0;
    int         maxTraceFiles         = 10;

    // --- 上传预算 ---
    uint64_t    uploadBudgetBytes     = 0;

    // 上传回调 (可选, 导出后调用, 由用户实现传输逻辑)
    std::function<void(const std::string& filePath)> uploadCallback;

    // 从 INI 文件加载配置 (缺失的字段保持默认值)
    // 文件不存在时返回默认配置, 不报错
    TELEMETRY_API static Config loadFromFile(const std::string& path);
};

// ============================================================
// Manager 单例
//
// 生命周期: init() → frameMark() x N → exportTrace() → shutdown()
// 后台采集线程以低优先级定期 drain 所有线程 ring buffer
// ============================================================
class TELEMETRY_API Manager {
public:
    static Manager& get();

    // 初始化方式 (三选一):
    //   init()                   - 自动查找 telemetry.ini (exe目录 → 当前目录), 找不到用默认配置
    //   init("path/telemetry.ini") - 从指定 INI 文件加载
    //   init(cfg)                - 直接传入代码构造的 Config
    void init();
    void init(const Config& cfg);
    void init(const std::string& configPath);
    void shutdown();
    bool isActive() const { return active_.load(std::memory_order_acquire); }

    // 注册线程缓冲区 (由 GetThreadState 自动调用)
    void registerThread(ThreadState* state);

    // 帧标记 + 卡顿检测
    void frameMark(const char* name = nullptr);

    // 手动导出 Chrome Trace JSON
    void exportTrace(const std::string& filename = "");

    // 统计
    uint64_t totalEvents() const { return totalEvents_.load(std::memory_order_relaxed); }
    uint64_t frameCount()  const { return frameCount_.load(std::memory_order_relaxed); }
    uint64_t totalDropped() const;

    const std::vector<StutterInfo>& stutters() const { return stutters_; }
    const Config& config() const { return config_; }

private:
    Manager() = default;
    ~Manager();
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    void drainThreadFunc();
    void drainAllBuffers();
    void exportChromeTrace(const std::string& path);
    void rollingExport();
    void cleanOldTraces();
    std::string gatherContexts() const;

    Config config_;
    std::atomic<bool> active_{false};
    std::thread drainThread_;

    mutable std::mutex threadsMtx_;
    std::vector<ThreadState*> threads_;

    std::mutex eventsMtx_;
    std::vector<Event> events_;

    int64_t lastFrameTs_ = 0;
    int64_t lastExportTs_ = 0;
    std::atomic<uint64_t> frameCount_{0};
    std::atomic<uint64_t> totalEvents_{0};

    // stutterOnly 模式: 滚动窗口 + 捕获触发
    std::vector<Event> recentEvents_;            // 滚动窗口 (最近 N ms 的事件)
    std::atomic<int64_t> captureUntilTs_{0};     // >0 时处于卡顿后捕获阶段
    void commitRecentEvents();                   // 将滚动窗口事件提交到 events_
    void trimRecentEvents(int64_t cutoffTs);     // 裁剪过旧的滚动窗口事件
    void closeOpenEvents(int64_t endTs);         // 为未闭合的 Zone/Lock 合成 End

    uint64_t uploadedBytes_ = 0;
    std::vector<std::string> exportedFiles_;

    std::mutex stutterMtx_;
    std::vector<StutterInfo> stutters_;
};

} // namespace telemetry
