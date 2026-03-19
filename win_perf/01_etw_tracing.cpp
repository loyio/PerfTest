/*
 * 01_etw_tracing.cpp - Windows ETW (Event Tracing for Windows) 示例
 *
 * 学习要点:
 * 1. ETW 是 Windows 上最强大的性能分析机制
 * 2. 如何在应用中嵌入自定义 ETW 标记
 * 3. 配合 Windows Performance Recorder (WPR) 使用
 * 4. TraceLogging API 的基本用法
 *
 * 工具链:
 * - Windows Performance Recorder (WPR) 录制追踪
 * - Windows Performance Analyzer (WPA) 分析追踪
 * - PerfView (Microsoft 开源工具)
 *
 * 注意: 需要管理员权限运行 WPR
 */

#include "profiler.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <cmath>

#ifdef _WIN32
#include <Windows.h>
#include <evntrace.h>

// ============================================================
// 简化版 ETW 标记 - 使用 Windows 内置的 marker
// (完整 TraceLogging 需要 TraceLoggingProvider.h)
// ============================================================

// 使用 QueryPerformanceCounter 作为精确时间源
class ETWSimulator {
public:
    struct Event {
        int64_t timestamp;
        std::string name;
        std::string category;
        enum Type { BEGIN, END, INSTANT } type;
    };

    void begin(const std::string& name, const std::string& category = "default") {
        LARGE_INTEGER ts;
        QueryPerformanceCounter(&ts);
        events_.push_back({ts.QuadPart, name, category, Event::BEGIN});
    }

    void end(const std::string& name, const std::string& category = "default") {
        LARGE_INTEGER ts;
        QueryPerformanceCounter(&ts);
        events_.push_back({ts.QuadPart, name, category, Event::END});
    }

    void instant(const std::string& name, const std::string& category = "default") {
        LARGE_INTEGER ts;
        QueryPerformanceCounter(&ts);
        events_.push_back({ts.QuadPart, name, category, Event::INSTANT});
    }

    void report() const {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);

        std::cout << "\n  ETW-style Event Trace:\n";
        std::cout << "  " << std::string(60, '-') << "\n";

        for (const auto& evt : events_) {
            double timeMs = (evt.timestamp * 1000.0) / freq.QuadPart;
            const char* typeStr = (evt.type == Event::BEGIN) ? "BEGIN" :
                                  (evt.type == Event::END)   ? "END  " : "MARK ";
            std::cout << "    [" << typeStr << "] "
                      << std::fixed << std::setprecision(3) << timeMs << " ms  "
                      << "[" << evt.category << "] " << evt.name << "\n";
        }

        // 计算配对的 BEGIN/END 持续时间
        std::cout << "\n  Duration Summary:\n";
        for (size_t i = 0; i < events_.size(); ++i) {
            if (events_[i].type == Event::BEGIN) {
                for (size_t j = i + 1; j < events_.size(); ++j) {
                    if (events_[j].type == Event::END &&
                        events_[j].name == events_[i].name) {
                        double durationUs = (events_[j].timestamp - events_[i].timestamp)
                                            * 1000000.0 / freq.QuadPart;
                        std::cout << "    " << events_[i].name << ": "
                                  << std::setprecision(1) << durationUs << " us\n";
                        break;
                    }
                }
            }
        }
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

// ============================================================
// RAII Scoped Event
// ============================================================
class ScopedEvent {
public:
    ScopedEvent(ETWSimulator& tracer, const std::string& name,
                const std::string& category = "default")
        : tracer_(tracer), name_(name), category_(category) {
        tracer_.begin(name_, category_);
    }
    ~ScopedEvent() {
        tracer_.end(name_, category_);
    }
private:
    ETWSimulator& tracer_;
    std::string name_;
    std::string category_;
};

// ============================================================
// 模拟场景: 一帧游戏的追踪
// ============================================================
void simulatePhysics(ETWSimulator& tracer) {
    ScopedEvent evt(tracer, "Physics", "GameFrame");
    // 模拟物理计算
    volatile double sum = 0;
    for (int i = 0; i < 100000; ++i) {
        sum += std::sin(i * 0.001);
    }
}

void simulateCollision(ETWSimulator& tracer) {
    ScopedEvent evt(tracer, "Collision", "GameFrame");
    volatile double sum = 0;
    for (int i = 0; i < 50000; ++i) {
        sum += std::cos(i * 0.002);
    }
}

void simulateRender(ETWSimulator& tracer) {
    ScopedEvent evt(tracer, "Render", "GameFrame");

    {
        ScopedEvent cull(tracer, "Culling", "Render");
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    {
        ScopedEvent draw(tracer, "DrawCalls", "Render");
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    {
        ScopedEvent post(tracer, "PostProcess", "Render");
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
}

void simulateAI(ETWSimulator& tracer) {
    ScopedEvent evt(tracer, "AI", "GameFrame");
    std::this_thread::sleep_for(std::chrono::microseconds(400));
}

void demo_frame_trace() {
    perf::printHeader("Game Frame Trace (ETW-style)");

    ETWSimulator tracer;

    // 追踪一帧
    {
        ScopedEvent frame(tracer, "Frame", "GameLoop");

        simulatePhysics(tracer);
        simulateCollision(tracer);
        simulateAI(tracer);

        tracer.instant("BeginRender", "GameFrame");
        simulateRender(tracer);
    }

    tracer.report();
}

// ============================================================
// 演示: 如何使用真实的 WPR/WPA
// ============================================================
void demo_wpr_instructions() {
    perf::printHeader("How to Use Windows Performance Recorder (WPR)");

    std::cout << R"(
  Windows 性能工具使用指南:

  1. 录制追踪:
     > wpr -start GeneralProfile -filemode
     > [运行你的程序]
     > wpr -stop trace.etl

  2. 分析追踪:
     > wpa trace.etl

  3. WPA 中关注的面板:
     - CPU Usage (Sampled)  -> 找到热点函数
     - CPU Usage (Precise)  -> 线程调度分析
     - Disk I/O             -> 文件读写延迟
     - Memory               -> 内存使用趋势
     - Generic Events       -> 自定义 ETW 事件

  4. 使用 PerfView (推荐):
     - 下载: https://github.com/microsoft/perfview
     - 功能: CPU 采样, GC 分析, 火焰图
     - 命令: PerfView collect /ThreadTime /GCCollectOnly

  5. Visual Studio 内置分析器:
     - Debug -> Performance Profiler
     - CPU Usage / Memory Usage / .NET Async

  6. 关键指标:
     - CPU Utilization    : 各核心使用率
     - Context Switches   : 线程切换次数 (高 -> 锁竞争)
     - Page Faults        : 缺页中断 (高 -> 内存不足)
     - Disk I/O latency   : 磁盘延迟 (高 -> I/O 卡顿)
)";
}

#endif // _WIN32

int main() {
    perf::initConsole();
    std::cout << "===== 01: ETW Tracing Tutorial =====\n";

#ifdef _WIN32
    demo_frame_trace();
    demo_wpr_instructions();
#else
    std::cout << "This example is Windows-only.\n";
#endif

    return 0;
}
