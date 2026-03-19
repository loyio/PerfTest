#pragma once
#include <chrono>
#include <string>
#include <iostream>
#include <iomanip>
#include <vector>
#include <functional>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace perf {

// ============================================================
// initConsole - 设置控制台 UTF-8 输出，避免中文乱码
// 在 main() 开头调用: perf::initConsole();
// ============================================================
inline void initConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    // 同时设置输入也为 UTF-8
    SetConsoleCP(CP_UTF8);
#endif
}

// ============================================================
// ScopedTimer - 自动计时的 RAII 工具
// 用法: { ScopedTimer t("MyFunction"); ... }
// 离开作用域时自动输出耗时
// ============================================================
class ScopedTimer {
public:
    explicit ScopedTimer(const std::string& name)
        : name_(name)
        , start_(std::chrono::high_resolution_clock::now())
    {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
        std::cout << "[Timer] " << name_ << ": "
                  << us << " us ("
                  << std::fixed << std::setprecision(3) << us / 1000.0 << " ms)\n";
    }

    // 禁止拷贝
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};

// ============================================================
// Benchmark - 多次执行函数并统计平均/最小/最大耗时
// ============================================================
struct BenchmarkResult {
    double avg_us  = 0;
    double min_us  = 0;
    double max_us  = 0;
    int    iterations = 0;
};

inline BenchmarkResult benchmark(const std::string& name,
                                  std::function<void()> func,
                                  int iterations = 100)
{
    std::vector<double> times;
    times.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000.0;
        times.push_back(us);
    }

    double total = 0, mn = times[0], mx = times[0];
    for (double t : times) {
        total += t;
        if (t < mn) mn = t;
        if (t > mx) mx = t;
    }

    BenchmarkResult result;
    result.avg_us = total / iterations;
    result.min_us = mn;
    result.max_us = mx;
    result.iterations = iterations;

    std::cout << "[Benchmark] " << name << "\n"
              << "  iterations: " << iterations << "\n"
              << "  avg: " << std::fixed << std::setprecision(2) << result.avg_us << " us\n"
              << "  min: " << result.min_us << " us\n"
              << "  max: " << result.max_us << " us\n";

    return result;
}

// ============================================================
// 打印分隔线和标题，方便输出对比
// ============================================================
inline void printHeader(const std::string& title) {
    std::cout << "\n========================================\n"
              << "  " << title << "\n"
              << "========================================\n\n";
}

inline void printComparison(const std::string& label_a, double us_a,
                             const std::string& label_b, double us_b)
{
    double ratio = (us_b > 0) ? (us_a / us_b) : 0;
    std::cout << "\n--- Comparison ---\n"
              << "  " << label_a << ": " << std::fixed << std::setprecision(2) << us_a << " us\n"
              << "  " << label_b << ": " << us_b << " us\n"
              << "  Speedup: " << std::setprecision(2) << ratio << "x\n\n";
}

} // namespace perf
