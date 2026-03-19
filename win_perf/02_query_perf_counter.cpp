/*
 * 02_query_perf_counter.cpp - Windows 高精度计时器使用
 *
 * 学习要点:
 * 1. QueryPerformanceCounter (QPC) - 最精确的 Windows 计时器
 * 2. 不同计时方式的精度对比
 * 3. CPU 时钟周期计数 (__rdtsc)
 * 4. 正确的性能测量方法论
 *
 * 性能优化的第一步是正确测量!
 */

#include "profiler.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <Windows.h>
#include <mmsystem.h>
#include <intrin.h> // __rdtsc
#pragma comment(lib, "winmm.lib")

// ============================================================
// 测试1: 不同计时器的精度对比
// ============================================================
void test_timer_resolution() {
    perf::printHeader("Timer Resolution Comparison");

    // 1. GetTickCount64 (精度 ~15.6ms)
    {
        ULONGLONG t1 = GetTickCount64();
        // 最小可测量间隔
        ULONGLONG t2;
        int spins = 0;
        do {
            t2 = GetTickCount64();
            spins++;
        } while (t2 == t1);
        std::cout << "  GetTickCount64:\n"
                  << "    Resolution: " << (t2 - t1) << " ms\n"
                  << "    Spins: " << spins << "\n\n";
    }

    // 2. timeGetTime (精度 ~1ms with timeBeginPeriod)
    {
        timeBeginPeriod(1);
        DWORD t1 = timeGetTime();
        DWORD t2;
        int spins = 0;
        do {
            t2 = timeGetTime();
            spins++;
        } while (t2 == t1);
        timeEndPeriod(1);
        std::cout << "  timeGetTime (with timeBeginPeriod(1)):\n"
                  << "    Resolution: " << (t2 - t1) << " ms\n"
                  << "    Spins: " << spins << "\n\n";
    }

    // 3. QueryPerformanceCounter (精度 ~100ns)
    {
        LARGE_INTEGER freq, t1, t2;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t1);
        int spins = 0;
        do {
            QueryPerformanceCounter(&t2);
            spins++;
        } while (t2.QuadPart == t1.QuadPart);
        double ns = (t2.QuadPart - t1.QuadPart) * 1e9 / freq.QuadPart;
        std::cout << "  QueryPerformanceCounter:\n"
                  << "    Frequency: " << freq.QuadPart << " Hz\n"
                  << "    Resolution: " << std::setprecision(1) << ns << " ns\n"
                  << "    Spins: " << spins << "\n\n";
    }

    // 4. std::chrono::high_resolution_clock
    {
        auto t1 = std::chrono::high_resolution_clock::now();
        decltype(t1) t2;
        int spins = 0;
        do {
            t2 = std::chrono::high_resolution_clock::now();
            spins++;
        } while (t2 == t1);
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
        std::cout << "  std::chrono::high_resolution_clock:\n"
                  << "    Resolution: " << ns << " ns\n"
                  << "    Spins: " << spins << "\n"
                  << "    (On MSVC, this wraps QPC)\n\n";
    }

    // 5. __rdtsc (CPU 时钟周期)
    {
        unsigned __int64 t1 = __rdtsc();
        unsigned __int64 t2;
        int spins = 0;
        do {
            t2 = __rdtsc();
            spins++;
        } while (t2 == t1);
        std::cout << "  __rdtsc (CPU cycles):\n"
                  << "    Resolution: " << (t2 - t1) << " cycles\n"
                  << "    Spins: " << spins << "\n"
                  << "    注意: rdtsc 受 CPU 频率缩放影响, 不适合跨核测量\n\n";
    }
}

// ============================================================
// 测试2: QPC 实际用法 - 精确测量函数执行时间
// ============================================================
class QPCTimer {
public:
    QPCTimer() {
        QueryPerformanceFrequency(&freq_);
    }

    void start() {
        QueryPerformanceCounter(&start_);
    }

    double elapsedUs() const {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (now.QuadPart - start_.QuadPart) * 1e6 / freq_.QuadPart;
    }

    double elapsedMs() const {
        return elapsedUs() / 1000.0;
    }

private:
    LARGE_INTEGER freq_;
    LARGE_INTEGER start_;
};

void test_qpc_usage() {
    perf::printHeader("QPC Practical Usage");

    QPCTimer timer;
    constexpr int ITERS = 1000000;

    // 测量数学运算
    {
        timer.start();
        volatile double sum = 0;
        for (int i = 0; i < ITERS; ++i) {
            sum += std::sin(i * 0.001) * std::cos(i * 0.002);
        }
        double elapsed = timer.elapsedUs();
        std::cout << "  sin*cos x " << ITERS << ": "
                  << std::setprecision(1) << elapsed << " us ("
                  << std::setprecision(2) << elapsed / ITERS * 1000 << " ns/op)\n";
    }

    // 测量内存分配
    {
        timer.start();
        for (int i = 0; i < 10000; ++i) {
            void* p = malloc(1024);
            free(p);
        }
        double elapsed = timer.elapsedUs();
        std::cout << "  malloc/free 1KB x 10000: "
                  << std::setprecision(1) << elapsed << " us ("
                  << std::setprecision(2) << elapsed / 10000 * 1000 << " ns/op)\n";
    }

    // 测量 mutex lock/unlock
    {
        CRITICAL_SECTION cs;
        InitializeCriticalSection(&cs);
        timer.start();
        for (int i = 0; i < ITERS; ++i) {
            EnterCriticalSection(&cs);
            LeaveCriticalSection(&cs);
        }
        double elapsed = timer.elapsedUs();
        DeleteCriticalSection(&cs);
        std::cout << "  CriticalSection lock/unlock x " << ITERS << ": "
                  << std::setprecision(1) << elapsed << " us ("
                  << std::setprecision(2) << elapsed / ITERS * 1000 << " ns/op)\n";
    }
}

// ============================================================
// 测试3: 正确的 benchmark 方法论
// ============================================================
void test_benchmark_methodology() {
    perf::printHeader("Benchmark Methodology: Warmup + Multiple Runs");

    constexpr int N = 500000;
    auto workload = [N]() {
        volatile double sum = 0;
        for (int i = 0; i < N; ++i) {
            sum += std::sqrt(static_cast<double>(i));
        }
    };

    QPCTimer timer;
    constexpr int WARMUP = 3;
    constexpr int RUNS = 10;
    std::vector<double> times;

    // Warmup (让 CPU 频率稳定, 缓存预热)
    std::cout << "  Warming up (" << WARMUP << " runs)...\n";
    for (int i = 0; i < WARMUP; ++i) {
        workload();
    }

    // 正式测量
    for (int i = 0; i < RUNS; ++i) {
        timer.start();
        workload();
        times.push_back(timer.elapsedUs());
    }

    std::sort(times.begin(), times.end());
    double sum = std::accumulate(times.begin(), times.end(), 0.0);

    std::cout << "\n  Results (" << RUNS << " runs):\n";
    for (int i = 0; i < RUNS; ++i) {
        std::cout << "    Run " << (i+1) << ": "
                  << std::setprecision(1) << times[i] << " us\n";
    }

    std::cout << "\n  Statistics:\n"
              << "    Mean:   " << sum / RUNS << " us\n"
              << "    Median: " << times[RUNS/2] << " us (更可靠!)\n"
              << "    Min:    " << times.front() << " us\n"
              << "    Max:    " << times.back() << " us\n"
              << "    Range:  " << (times.back() - times.front()) << " us\n";

    // 去除极值后的平均 (trimmed mean)
    double trimmedSum = 0;
    int trimmedCount = 0;
    for (int i = 1; i < RUNS - 1; ++i) { // 去掉最高和最低
        trimmedSum += times[i];
        trimmedCount++;
    }
    std::cout << "    Trimmed mean: " << trimmedSum / trimmedCount << " us\n";

    std::cout << "\n  Methodology Tips:\n"
              << "  1. 总是 warmup (CPU频率/缓存/分支预测器)\n"
              << "  2. 多次运行取中位数 (而非平均值)\n"
              << "  3. 关闭其他程序减少干扰\n"
              << "  4. 使用 Release 模式 + 优化\n"
              << "  5. 注意编译器可能优化掉你的测试代码 (用 volatile)\n";
}

// ============================================================
// 测试4: Sleep 精度分析
// ============================================================
void test_sleep_precision() {
    perf::printHeader("Sleep Precision Analysis");

    QPCTimer timer;
    std::vector<double> actual_ms;

    struct TestCase {
        int target_ms;
        int count;
    };

    std::vector<TestCase> cases = {{1, 20}, {5, 20}, {10, 10}, {16, 10}};

    for (auto& tc : cases) {
        actual_ms.clear();
        for (int i = 0; i < tc.count; ++i) {
            timer.start();
            Sleep(tc.target_ms);
            actual_ms.push_back(timer.elapsedMs());
        }

        double avg = std::accumulate(actual_ms.begin(), actual_ms.end(), 0.0) / actual_ms.size();
        double minVal = *std::min_element(actual_ms.begin(), actual_ms.end());
        double maxVal = *std::max_element(actual_ms.begin(), actual_ms.end());

        std::cout << "  Sleep(" << tc.target_ms << "ms):\n"
                  << "    Actual avg: " << std::setprecision(2) << avg << " ms\n"
                  << "    Range: [" << minVal << ", " << maxVal << "] ms\n"
                  << "    Error: " << std::setprecision(1) << (avg - tc.target_ms) << " ms\n\n";
    }

    std::cout << "  提高 Sleep 精度的方法:\n"
              << "  1. timeBeginPeriod(1) - 将系统时钟精度提高到 1ms\n"
              << "  2. CreateWaitableTimerEx + HIGH_RESOLUTION - Win10+\n"
              << "  3. Sleep + spin-wait 混合策略\n"
              << "  4. MMCSS (Multimedia Class Scheduler Service)\n";
}

#endif // _WIN32

int main() {
    perf::initConsole();
    std::cout << "===== 02: QueryPerformanceCounter Tutorial =====\n";
    std::cout << "Precise timing is the foundation of performance optimization\n\n";

#ifdef _WIN32
    test_timer_resolution();
    test_qpc_usage();
    test_benchmark_methodology();
    test_sleep_precision();
#else
    std::cout << "This example is Windows-only.\n";
#endif

    return 0;
}
