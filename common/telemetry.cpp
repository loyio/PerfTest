/*
 * telemetry.cpp - Telemetry 系统实现
 *
 * 包含:
 *   - 线程状态管理 (thread_local)
 *   - Manager 生命周期 (init / drain / shutdown)
 *   - Chrome Trace Event JSON 导出
 *   - 卡顿检测逻辑
 */

#include "telemetry.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>

#ifdef _WIN32
    #include <direct.h>     // _mkdir
#else
    #include <sys/stat.h>   // mkdir
    #include <unistd.h>     // unlink
#endif

namespace telemetry {

// ============================================================
// 文件系统工具 (代替 std::filesystem, 兼容 C++14 / XP)
// ============================================================
static void EnsureDirectory(const std::string& dir) {
#ifdef _WIN32
    ::CreateDirectoryA(dir.c_str(), NULL);
#else
    ::mkdir(dir.c_str(), 0755);
#endif
}

static uint64_t GetFileSize(const std::string& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (::GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER li;
        li.HighPart = fad.nFileSizeHigh;
        li.LowPart  = fad.nFileSizeLow;
        return static_cast<uint64_t>(li.QuadPart);
    }
    return 0;
#else
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        return static_cast<uint64_t>(st.st_size);
    }
    return 0;
#endif
}

static void RemoveFile(const std::string& path) {
#ifdef _WIN32
    ::DeleteFileA(path.c_str());
#else
    ::unlink(path.c_str());
#endif
}

static bool FileExists(const std::string& path) {
#ifdef _WIN32
    DWORD attr = ::GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode));
#endif
}

// 获取可执行文件所在目录
static std::string GetExeDirectory() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    DWORD len = ::GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len > 0) {
        std::string s(buf, len);
        auto pos = s.find_last_of("\\/");
        if (pos != std::string::npos) return s.substr(0, pos);
    }
#endif
    return ".";
}

// ============================================================
// INI 配置文件解析器
//
// 格式:
//   # 注释行
//   key = value
//
// 支持的 key 与 Config 字段一一对应 (见 telemetry.ini.example)
// 未识别的 key 静默跳过, 文件不存在返回默认配置
// ============================================================

// 去除首尾空白
static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool ParseBool(const std::string& val) {
    std::string v = Trim(val);
    return (v == "1" || v == "true" || v == "yes" || v == "on");
}

Config Config::loadFromFile(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;

    std::string line;
    while (std::getline(f, line)) {
        // 去除注释和空行
        auto commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        line = Trim(line);
        if (line.empty()) continue;

        // 拆分 key = value
        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eqPos));
        std::string val = Trim(line.substr(eqPos + 1));
        if (key.empty()) continue;

        // 匹配字段
        if (key == "stutter_threshold_ms") {
            cfg.stutterThresholdMs = static_cast<float>(atof(val.c_str()));
        } else if (key == "drain_interval_ms") {
            cfg.drainIntervalMs = static_cast<float>(atof(val.c_str()));
        } else if (key == "output_dir") {
            cfg.outputDir = val;
        } else if (key == "auto_export") {
            cfg.autoExport = ParseBool(val);
        } else if (key == "max_events") {
            cfg.maxEvents = static_cast<uint32_t>(atoi(val.c_str()));
        } else if (key == "stutter_only") {
            cfg.stutterOnly = ParseBool(val);
        } else if (key == "stutter_capture_before_ms") {
            cfg.stutterCaptureBeforeMs = static_cast<float>(atof(val.c_str()));
        } else if (key == "stutter_capture_after_ms") {
            cfg.stutterCaptureAfterMs = static_cast<float>(atof(val.c_str()));
        } else if (key == "auto_export_interval_sec") {
            cfg.autoExportIntervalSec = static_cast<float>(atof(val.c_str()));
        } else if (key == "max_trace_files") {
            cfg.maxTraceFiles = atoi(val.c_str());
        } else if (key == "upload_budget_bytes") {
            cfg.uploadBudgetBytes = static_cast<uint64_t>(
                strtoll(val.c_str(), nullptr, 10));
        }
        // 未识别的 key 静默跳过
    }

    return cfg;
}

// ============================================================
// Thread State 管理
// ============================================================
static thread_local ThreadState* t_state = nullptr;

ThreadState& GetThreadState() {
    if (!t_state) {
        t_state = new ThreadState();
        t_state->threadId = CurrentThreadId();
    }
    // Manager 激活后, 首次访问时自动注册
    if (!t_state->registered && Manager::get().isActive()) {
        t_state->registered = true;
        Manager::get().registerThread(t_state);
    }
    return *t_state;
}

void SetThreadName(const char* name) {
    auto& ts = GetThreadState();
    size_t len = strlen(name);
    if (len >= sizeof(ts.name)) len = sizeof(ts.name) - 1;
    memcpy(ts.name, name, len);
    ts.name[len] = '\0';
    ts.named = true;

    Event e{};
    e.type = EventType::ThreadName;
    e.threadId = ts.threadId;
    e.timestamp = Now();
    e.name = ts.name;  // 指向 ThreadState 自身存储
    e.color = 0;
    ts.buffer.push(e);
}

// ============================================================
// Manager 单例
// ============================================================
Manager& Manager::get() {
    static Manager instance;
    return instance;
}

Manager::~Manager() {
    shutdown();
}

// 从文件路径初始化
void Manager::init() {
    // 自动查找 telemetry.ini: exe目录 → 当前目录
    std::string exeDirIni = GetExeDirectory() + "\\telemetry.ini";
    if (FileExists(exeDirIni)) {
        init(Config::loadFromFile(exeDirIni));
        return;
    }
    if (FileExists("telemetry.ini")) {
        init(Config::loadFromFile("telemetry.ini"));
        return;
    }
    init(Config{});
}

void Manager::init(const std::string& configPath) {
    init(Config::loadFromFile(configPath));
}

void Manager::init(const Config& cfg) {
    if (active_.load()) return;

    config_ = cfg;
    events_.clear();
    events_.reserve(config_.maxEvents);
    stutters_.clear();
    frameCount_.store(0);
    totalEvents_.store(0);
    lastFrameTs_ = 0;

    EnsureDirectory(config_.outputDir);

    active_.store(true, std::memory_order_release);
    lastFrameTs_ = Now();
    lastExportTs_ = Now();

    drainThread_ = std::thread(&Manager::drainThreadFunc, this);

#ifdef _WIN32
    SetThreadPriority(drainThread_.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

void Manager::shutdown() {
    if (!active_.exchange(false)) return;

    if (drainThread_.joinable()) {
        drainThread_.join();
    }

    // 最终 drain
    drainAllBuffers();

    // 自动导出
    if (config_.autoExport && !events_.empty()) {
        exportTrace();
    }

    // 清理线程状态
    {
        std::lock_guard<std::mutex> lock(threadsMtx_);
        for (auto* ts : threads_) {
            if (ts == t_state) t_state = nullptr;
            delete ts;
        }
        threads_.clear();
    }
}

void Manager::registerThread(ThreadState* state) {
    std::lock_guard<std::mutex> lock(threadsMtx_);
    threads_.push_back(state);
}

// ============================================================
// 帧标记 + 卡顿检测
// ============================================================
void Manager::frameMark(const char* name) {
    if (!active_.load(std::memory_order_acquire)) return;

    int64_t now = Now();

    RecordFrameMark(name);

    if (lastFrameTs_ > 0) {
        float frameMs = static_cast<float>(now - lastFrameTs_) / 1000.0f;
        if (frameMs > config_.stutterThresholdMs) {
            StutterInfo si{};
            si.timestamp = now;
            si.frameTimeMs = frameMs;
            si.threadId = CurrentThreadId();
            si.frameIndex = frameCount_.load(std::memory_order_relaxed);
            si.context = gatherContexts();

            {
                std::lock_guard<std::mutex> lock(stutterMtx_);
                stutters_.push_back(si);
            }

            // stutterOnly 模式: 提交滚动窗口 + 开启后续捕获
            if (config_.stutterOnly) {
                int64_t afterUs = static_cast<int64_t>(
                    config_.stutterCaptureAfterMs * 1000.0f);
                {
                    std::lock_guard<std::mutex> lock(eventsMtx_);
                    commitRecentEvents();
                    // 必须在同一把锁内设置, 否则 drain 线程
                    // 可能在 commit 后、标志设置前运行
                    captureUntilTs_.store(now + afterUs,
                                          std::memory_order_release);
                }
            }
        }
    }

    lastFrameTs_ = now;
    frameCount_.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================
// 后台采集
// ============================================================
void Manager::drainThreadFunc() {
    while (active_.load(std::memory_order_acquire)) {
        drainAllBuffers();

        // 滚动自动导出
        if (config_.autoExportIntervalSec > 0) {
            int64_t now = Now();
            float elapsedSec = static_cast<float>(now - lastExportTs_) / 1000000.0f;
            if (elapsedSec >= config_.autoExportIntervalSec) {
                rollingExport();
                lastExportTs_ = now;
            }
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(config_.drainIntervalMs)));
    }
}

void Manager::drainAllBuffers() {
    std::vector<Event> batch;
    batch.reserve(4096);

    {
        std::lock_guard<std::mutex> lock(threadsMtx_);
        for (auto* ts : threads_) {
            Event e;
            while (ts->buffer.pop(e)) {
                batch.push_back(e);
            }
        }
    }

    if (batch.empty()) return;

    totalEvents_.fetch_add(static_cast<uint64_t>(batch.size()),
                           std::memory_order_relaxed);

    if (!config_.stutterOnly) {
        // 正常模式: 全部写入 events_
        std::lock_guard<std::mutex> lock(eventsMtx_);
        for (auto& e : batch) {
            if (events_.size() < config_.maxEvents) {
                events_.push_back(e);
            }
        }
    } else {
        // stutterOnly 模式
        std::lock_guard<std::mutex> lock(eventsMtx_);

        int64_t captureUntil = captureUntilTs_.load(std::memory_order_acquire);
        int64_t now = Now();

        for (auto& e : batch) {
            // ThreadName 事件始终保留 (元数据)
            if (e.type == EventType::ThreadName) {
                events_.push_back(e);
                continue;
            }

            // 用事件自身的时间戳判断, 而不是当前 wall-clock
            // 因为 batch 中的事件可能来自 drainInterval(500ms) 之前,
            // 使用 now 会把本属于捕获窗口内的事件错误路由到滚动窗口
            bool inCapture = (captureUntil > 0 && e.timestamp <= captureUntil);

            if (inCapture) {
                if (events_.size() < config_.maxEvents) {
                    events_.push_back(e);
                }
            } else {
                // 非捕获期: 写入滚动窗口
                recentEvents_.push_back(e);
            }
        }

        // 裁剪滚动窗口: 丢弃 captureBeforeMs 之前的事件
        int64_t captureBeforeUs = static_cast<int64_t>(
            config_.stutterCaptureBeforeMs * 1000.0f);
        trimRecentEvents(now - captureBeforeUs);

        // 如果捕获窗口已过期 (wall-clock 已超过), 合成未闭合事件
        if (captureUntil > 0 && now > captureUntil) {
            closeOpenEvents(captureUntil);
            captureUntilTs_.store(0, std::memory_order_release);
        }
    }
}

uint64_t Manager::totalDropped() const {
    uint64_t total = 0;
    std::lock_guard<std::mutex> lock(threadsMtx_);
    for (const auto* ts : threads_) {
        total += ts->buffer.dropped();
    }
    return total;
}

// ============================================================
// 导出
// ============================================================
void Manager::exportTrace(const std::string& filename) {
    std::string path;
    if (filename.empty()) {
        auto sysNow = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(sysNow);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &tt);
#else
        localtime_r(&tt, &tm_buf);
#endif
        char buf[128];
        snprintf(buf, sizeof(buf), "trace_%04d%02d%02d_%02d%02d%02d.json",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
        path = config_.outputDir + "/" + buf;
    } else {
        path = filename;
    }

    {
        std::lock_guard<std::mutex> lock(eventsMtx_);
        exportChromeTrace(path);
    }

    exportedFiles_.push_back(path);
    cleanOldTraces();

    if (config_.uploadCallback) {
        uint64_t fileSize = GetFileSize(path);
        if (fileSize > 0) {
            if (config_.uploadBudgetBytes == 0 ||
                uploadedBytes_ + fileSize <= config_.uploadBudgetBytes) {
                uploadedBytes_ += fileSize;
                config_.uploadCallback(path);
            }
        }
    }
}

// ============================================================
// Chrome Trace Event JSON 格式导出
//
// 输出格式兼容:
//   - chrome://tracing
//   - Perfetto UI (https://ui.perfetto.dev)
//   - Speedscope
// ============================================================
void Manager::exportChromeTrace(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return;

    uint32_t pid = 0;
#ifdef _WIN32
    pid = ::GetCurrentProcessId();
#endif

    f << "{\"traceEvents\":[\n";

    bool first = true;
    auto comma = [&]() {
        if (!first) f << ",\n";
        first = false;
    };

    for (const auto& e : events_) {
        switch (e.type) {
        case EventType::ZoneBegin:
            comma();
            f << "{\"ph\":\"B\",\"name\":\"" << (e.name ? e.name : "?")
              << "\",\"cat\":\"zone\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"ts\":" << e.timestamp << "}";
            break;

        case EventType::ZoneEnd:
            comma();
            f << "{\"ph\":\"E\",\"cat\":\"zone\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"ts\":" << e.timestamp << "}";
            break;

        case EventType::Frame:
            comma();
            f << "{\"ph\":\"i\",\"name\":\"" << (e.name ? e.name : "Frame")
              << "\",\"cat\":\"frame\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"ts\":" << e.timestamp << ",\"s\":\"g\"}";
            break;

        case EventType::LockWait:
            comma();
            f << "{\"ph\":\"B\",\"name\":\"Wait:" << (e.name ? e.name : "?")
              << "\",\"cat\":\"lock\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"ts\":" << e.timestamp
              << ",\"args\":{\"lock_id\":" << e.lockId << "}}";
            break;

        case EventType::LockAcquire:
            // 结束等待
            comma();
            f << "{\"ph\":\"E\",\"name\":\"Wait:" << (e.name ? e.name : "?")
              << "\",\"cat\":\"lock\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"ts\":" << e.timestamp
              << ",\"args\":{\"lock_id\":" << e.lockId << "}}";
            // 开始持有
            comma();
            f << "{\"ph\":\"B\",\"name\":\"Hold:" << (e.name ? e.name : "?")
              << "\",\"cat\":\"lock\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"ts\":" << e.timestamp
              << ",\"args\":{\"lock_id\":" << e.lockId << "}}";
            break;

        case EventType::LockRelease:
            comma();
            f << "{\"ph\":\"E\",\"name\":\"Hold:" << (e.name ? e.name : "?")
              << "\",\"cat\":\"lock\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"ts\":" << e.timestamp
              << ",\"args\":{\"lock_id\":" << e.lockId << "}}";
            break;

        case EventType::Msg:
            comma();
            f << "{\"ph\":\"i\",\"name\":\"" << (e.name ? e.name : "")
              << "\",\"cat\":\"msg\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"ts\":" << e.timestamp << ",\"s\":\"t\"}";
            break;

        case EventType::ThreadName:
            comma();
            f << "{\"ph\":\"M\",\"name\":\"thread_name\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"args\":{\"name\":\"" << (e.name ? e.name : "?") << "\"}}";
            break;

        case EventType::PlotValue:
            comma();
            f << "{\"ph\":\"C\",\"name\":\"" << (e.name ? e.name : "?")
              << "\",\"pid\":" << pid
              << ",\"tid\":" << e.threadId
              << ",\"ts\":" << e.timestamp
              << ",\"args\":{\"value\":" << e.plotVal << "}}";
            break;
        }
    }

    // 卡顿标记 (全局 instant 事件, 在所有线程上可见)
    // context 嵌入事件名, 在 Perfetto 时间线上直接可见
    {
        std::lock_guard<std::mutex> lock(stutterMtx_);
        for (const auto& s : stutters_) {
            comma();
            // 事件名: "STUTTER 52.3ms | IO: hero_skin.fbx | Render: DrawScene"
            f << "{\"ph\":\"i\",\"name\":\"STUTTER "
              << std::fixed << std::setprecision(1) << s.frameTimeMs << "ms";
            if (!s.context.empty()) {
                f << " | " << s.context;
            }
            f << "\",\"cat\":\"stutter\",\"pid\":" << pid
              << ",\"tid\":" << s.threadId
              << ",\"ts\":" << s.timestamp << ",\"s\":\"g\""
              << ",\"args\":{\"frame_ms\":" << s.frameTimeMs
              << ",\"frame_index\":" << s.frameIndex;
            if (!s.context.empty()) {
                f << ",\"context\":\"" << s.context << "\"";
            }
            f << "}}";
        }
    }

    // ---- 安全网: 为所有未闭合的 Zone / Lock 生成合成的 End 事件 ----
    // 无论事件如何路由, 导出时保证每个 "B" 都有对应的 "E"
    {
        // 更精确的栈跟踪: 记录实际 JSON 的 B 事件名称
        struct OpenB { std::string jsonName; };
        struct ThreadStacks {
            uint32_t tid;
            std::vector<OpenB> stack;  // 当前未关闭的 B 事件 (LIFO)
        };
        std::vector<ThreadStacks> perThread;

        auto findThread = [&](uint32_t tid) -> ThreadStacks& {
            for (auto& t : perThread) {
                if (t.tid == tid) return t;
            }
            perThread.push_back({tid, {}});
            return perThread.back();
        };

        int64_t lastTs = 0;
        for (const auto& e : events_) {
            if (e.timestamp > lastTs) lastTs = e.timestamp;
            const char* name = e.name ? e.name : "?";
            auto& st = findThread(e.threadId);

            switch (e.type) {
            case EventType::ZoneBegin:
                st.stack.push_back({name});
                break;
            case EventType::ZoneEnd:
                if (!st.stack.empty()) st.stack.pop_back();
                break;
            case EventType::LockWait:
                // JSON: B(Wait:xxx)
                st.stack.push_back({std::string("Wait:") + name});
                break;
            case EventType::LockAcquire:
                // JSON: E(Wait:xxx) + B(Hold:xxx)
                if (!st.stack.empty()) st.stack.pop_back();
                st.stack.push_back({std::string("Hold:") + name});
                break;
            case EventType::LockRelease:
                // JSON: E(Hold:xxx)
                if (!st.stack.empty()) st.stack.pop_back();
                break;
            default: break;
            }
        }

        if (lastTs == 0) lastTs = Now();
        int64_t closeTs = lastTs + 1;

        for (const auto& ts : perThread) {
            // LIFO 顺序关闭, 确保嵌套正确
            for (auto it = ts.stack.rbegin(); it != ts.stack.rend(); ++it) {
                comma();
                // 判断是 zone 还是 lock (通过名称前缀)
                bool isLock = (it->jsonName.compare(0, 5, "Wait:") == 0 ||
                               it->jsonName.compare(0, 5, "Hold:") == 0);
                f << "{\"ph\":\"E\",\"name\":\"" << it->jsonName
                  << "\",\"cat\":\"" << (isLock ? "lock" : "zone")
                  << "\",\"pid\":" << pid
                  << ",\"tid\":" << ts.tid
                  << ",\"ts\":" << closeTs << "}";
            }
        }
    }

    f << "\n],\n";
    f << "\"displayTimeUnit\":\"ms\",\n";
    f << "\"metadata\":{\"total_events\":" << totalEvents_.load()
      << ",\"total_frames\":" << frameCount_.load()
      << ",\"total_stutters\":" << stutters_.size() << "}\n";
    f << "}\n";
}

// ============================================================
// 滚动导出 (运行期间周期性导出 + 清空缓冲)
// ============================================================
void Manager::rollingExport() {
    std::string path;
    {
        auto sysNow = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(sysNow);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &tt);
#else
        localtime_r(&tt, &tm_buf);
#endif
        char buf[128];
        snprintf(buf, sizeof(buf), "trace_%04d%02d%02d_%02d%02d%02d.json",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
        path = config_.outputDir + "/" + buf;
    }

    {
        std::lock_guard<std::mutex> lock(eventsMtx_);
        if (events_.empty()) return;

        // stutterOnly 模式: 如果正在捕获中, 延迟导出
        // 避免一次卡顿的数据被切割到两个 JSON 文件中
        if (config_.stutterOnly) {
            int64_t captureUntil = captureUntilTs_.load(std::memory_order_acquire);
            if (captureUntil > 0) {
                return; // 正在捕获, 等下次再导出
            }
        }

        exportChromeTrace(path);
        events_.clear();
        events_.reserve(config_.maxEvents);
    }

    exportedFiles_.push_back(path);
    cleanOldTraces();

    // 上传 (带预算检查)
    if (config_.uploadCallback) {
        uint64_t fileSize = GetFileSize(path);
        if (fileSize > 0) {
            if (config_.uploadBudgetBytes == 0 ||
                uploadedBytes_ + fileSize <= config_.uploadBudgetBytes) {
                uploadedBytes_ += fileSize;
                config_.uploadCallback(path);
            }
        }
    }
}

// ============================================================
// 文件轮转: 保留最近 maxTraceFiles 个文件
// ============================================================
void Manager::cleanOldTraces() {
    while (static_cast<int>(exportedFiles_.size()) > config_.maxTraceFiles) {
        const auto& oldest = exportedFiles_.front();
        RemoveFile(oldest);
        exportedFiles_.erase(exportedFiles_.begin());
    }
}

// ============================================================
// stutterOnly: 将滚动窗口事件全部提交到 events_ (卡顿触发时调用)
// 调用方必须已持有 eventsMtx_
// ============================================================
void Manager::commitRecentEvents() {
    for (auto& e : recentEvents_) {
        if (events_.size() < config_.maxEvents) {
            events_.push_back(e);
        }
    }
    recentEvents_.clear();
}

// ============================================================
// stutterOnly: 裁剪滚动窗口, 只保留 cutoffTs 之后的事件
// 不会在 Zone 或 Lock 配对中间截断
// 调用方必须已持有 eventsMtx_
// ============================================================
void Manager::trimRecentEvents(int64_t cutoffTs) {
    if (recentEvents_.empty()) return;

    // 找安全截断点: 要求该点之前所有线程的 zone 和 lock 深度均为 0
    size_t safeCut = 0;

    struct ThreadDepth {
        uint32_t tid;
        int zoneDepth;
        int lockDepth;  // LockWait +1, LockAcquire net 0, LockRelease -1
    };
    std::vector<ThreadDepth> stacks;

    auto findDepth = [&](uint32_t tid) -> ThreadDepth& {
        for (auto& s : stacks) {
            if (s.tid == tid) return s;
        }
        stacks.push_back({tid, 0, 0});
        return stacks.back();
    };

    for (size_t i = 0; i < recentEvents_.size(); i++) {
        const auto& e = recentEvents_[i];
        if (e.timestamp >= cutoffTs) break;

        auto& st = findDepth(e.threadId);
        switch (e.type) {
        case EventType::ZoneBegin:   st.zoneDepth++; break;
        case EventType::ZoneEnd:     if (st.zoneDepth > 0) st.zoneDepth--; break;
        case EventType::LockWait:    st.lockDepth++; break;
        case EventType::LockAcquire:
            if (st.lockDepth > 0) st.lockDepth--;
            st.lockDepth++;
            break;
        case EventType::LockRelease: if (st.lockDepth > 0) st.lockDepth--; break;
        default: break;
        }

        bool allClosed = true;
        for (const auto& s : stacks) {
            if (s.zoneDepth > 0 || s.lockDepth > 0) {
                allClosed = false;
                break;
            }
        }
        if (allClosed) {
            safeCut = i + 1;
        }
    }

    if (safeCut > 0) {
        recentEvents_.erase(recentEvents_.begin(),
                            recentEvents_.begin() + static_cast<ptrdiff_t>(safeCut));
    }
}

// ============================================================
// stutterOnly: 捕获窗口关闭时, 为 events_ 中未闭合的 Zone 和 Lock
// 合成关闭事件 (ZoneEnd / LockAcquire+LockRelease / LockRelease)
// 调用方必须已持有 eventsMtx_
// ============================================================
void Manager::closeOpenEvents(int64_t endTs) {
    struct OpenEntry {
        EventType type;
        const char* name;
        uint32_t lockId;
    };
    struct ThreadStack {
        uint32_t tid;
        std::vector<OpenEntry> stack;
    };
    std::vector<ThreadStack> stacks;

    auto findStack = [&](uint32_t tid) -> ThreadStack& {
        for (auto& s : stacks) {
            if (s.tid == tid) return s;
        }
        stacks.push_back({tid, {}});
        return stacks.back();
    };

    for (const auto& e : events_) {
        auto& st = findStack(e.threadId);
        switch (e.type) {
        case EventType::ZoneBegin:
            st.stack.push_back({e.type, e.name, 0});
            break;
        case EventType::ZoneEnd:
            if (!st.stack.empty()) st.stack.pop_back();
            break;
        case EventType::LockWait:
            st.stack.push_back({e.type, e.name, e.lockId});
            break;
        case EventType::LockAcquire:
            if (!st.stack.empty()) st.stack.pop_back();
            st.stack.push_back({EventType::LockAcquire, e.name, e.lockId});
            break;
        case EventType::LockRelease:
            if (!st.stack.empty()) st.stack.pop_back();
            break;
        default: break;
        }
    }

    // LIFO 顺序关闭
    for (const auto& ts : stacks) {
        for (auto it = ts.stack.rbegin(); it != ts.stack.rend(); ++it) {
            if (events_.size() >= config_.maxEvents) break;

            if (it->type == EventType::ZoneBegin) {
                Event synth{};
                synth.type = EventType::ZoneEnd;
                synth.threadId = ts.tid;
                synth.timestamp = endTs;
                synth.name = nullptr;
                events_.push_back(synth);
            } else if (it->type == EventType::LockWait) {
                // 未获取的 Wait: 生成 LockAcquire(关闭Wait) + LockRelease(关闭Hold)
                Event acq{};
                acq.type = EventType::LockAcquire;
                acq.threadId = ts.tid;
                acq.timestamp = endTs;
                acq.name = it->name;
                acq.lockId = it->lockId;
                events_.push_back(acq);

                if (events_.size() < config_.maxEvents) {
                    Event rel{};
                    rel.type = EventType::LockRelease;
                    rel.threadId = ts.tid;
                    rel.timestamp = endTs + 1;
                    rel.name = it->name;
                    rel.lockId = it->lockId;
                    events_.push_back(rel);
                }
            } else if (it->type == EventType::LockAcquire) {
                // 未释放的 Hold: 生成 LockRelease
                Event rel{};
                rel.type = EventType::LockRelease;
                rel.threadId = ts.tid;
                rel.timestamp = endTs;
                rel.name = it->name;
                rel.lockId = it->lockId;
                events_.push_back(rel);
            }
        }
    }
}

// ============================================================
// 收集各线程当前上下文 (用于卡顿诊断)
// ============================================================
std::string Manager::gatherContexts() const {
    std::string result;
    std::lock_guard<std::mutex> lock(threadsMtx_);
    for (const auto* ts : threads_) {
        const char* ctx = ts->context;  // relaxed read, approximate is fine
        if (ctx) {
            if (!result.empty()) result += " | ";
            if (ts->named) {
                result += ts->name;
                result += ": ";
            }
            result += ctx;
        }
    }
    return result;
}

} // namespace telemetry
