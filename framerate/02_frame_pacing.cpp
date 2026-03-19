/*
 * 02_frame_pacing.cpp - 帧步调 (Frame Pacing) 问题分析
 *
 * 学习要点:
 * 1. 什么是帧步调: 每帧间隔的均匀性
 * 2. 帧步调不良的表现: 即使 FPS 达标也感觉不流畅
 * 3. CPU/GPU 气泡 (Bubble) 的产生
 * 4. 使用 QueryPerformanceCounter 精确计时
 * 5. 展示 99th percentile frame time 的重要性
 *
 * "帧率60但还是感觉卡" 的常见原因就是帧步调问题
 */

#include "profiler.h"
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>
#include <iostream>
#include <iomanip>

#ifdef _WIN32
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double>;

// ============================================================
// 帧时间分析器 - 分析帧步调质量
// ============================================================
class FramePacingAnalyzer {
public:
    void record(double frameTimeMs) {
        frameTimes_.push_back(frameTimeMs);
    }

    void analyze(const std::string& label) const {
        if (frameTimes_.empty()) return;

        auto sorted = frameTimes_;
        std::sort(sorted.begin(), sorted.end());

        double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
        double avg = sum / sorted.size();

        // 计算百分位
        double p50 = percentile(sorted, 50);
        double p90 = percentile(sorted, 90);
        double p95 = percentile(sorted, 95);
        double p99 = percentile(sorted, 99);
        double maxVal = sorted.back();

        // 计算标准差 (帧步调质量指标)
        double variance = 0;
        for (double t : sorted) {
            double diff = t - avg;
            variance += diff * diff;
        }
        double stddev = std::sqrt(variance / sorted.size());

        // 计算连续帧差异 (jitter)
        double maxJitter = 0;
        double avgJitter = 0;
        for (size_t i = 1; i < frameTimes_.size(); ++i) {
            double jitter = std::abs(frameTimes_[i] - frameTimes_[i-1]);
            avgJitter += jitter;
            maxJitter = std::max(maxJitter, jitter);
        }
        if (frameTimes_.size() > 1) avgJitter /= (frameTimes_.size() - 1);

        // 计算 "卡顿帧" 数量 (超过平均值 2 倍的帧)
        int stutterFrames = 0;
        for (double t : frameTimes_) {
            if (t > avg * 2.0) stutterFrames++;
        }

        std::cout << "  [" << label << "] Frame Pacing Analysis (" << frameTimes_.size() << " frames)\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "    Average:     " << avg << " ms (" << (avg > 0 ? 1000.0 / avg : 0) << " FPS)\n";
        std::cout << "    P50:         " << p50 << " ms\n";
        std::cout << "    P90:         " << p90 << " ms\n";
        std::cout << "    P95:         " << p95 << " ms\n";
        std::cout << "    P99:         " << p99 << " ms  <-- 关注这个!\n";
        std::cout << "    Max:         " << maxVal << " ms\n";
        std::cout << "    Stddev:      " << stddev << " ms\n";
        std::cout << "    Avg jitter:  " << avgJitter << " ms\n";
        std::cout << "    Max jitter:  " << maxJitter << " ms\n";
        std::cout << "    Stutter frames (>2x avg): " << stutterFrames
                  << " (" << (100.0 * stutterFrames / frameTimes_.size()) << "%)\n\n";
    }

    void clear() { frameTimes_.clear(); }

private:
    std::vector<double> frameTimes_;

    static double percentile(const std::vector<double>& sorted, double p) {
        size_t index = static_cast<size_t>((p / 100.0) * (sorted.size() - 1));
        return sorted[std::min(index, sorted.size() - 1)];
    }
};

// ============================================================
// 场景1: 理想帧步调 - 均匀间隔
// ============================================================
void demo_ideal_pacing() {
    perf::printHeader("Ideal Frame Pacing");

    FramePacingAnalyzer analyzer;
    constexpr int FRAMES = 200;
    constexpr double TARGET_MS = 16.667; // 60 FPS

    for (int i = 0; i < FRAMES; ++i) {
        auto start = Clock::now();

        // 精确等待
        while (Duration(Clock::now() - start).count() * 1000.0 < TARGET_MS) {
            // spin-wait for exact timing
        }

        double dt = Duration(Clock::now() - start).count() * 1000.0;
        analyzer.record(dt);
    }

    analyzer.analyze("Ideal Pacing");
}

// ============================================================
// 场景2: 有周期性卡顿的帧步调
// ============================================================
void demo_periodic_stutter() {
    perf::printHeader("Periodic Stutter (e.g., GC, shader compilation)");

    FramePacingAnalyzer analyzer;
    constexpr int FRAMES = 200;
    constexpr double TARGET_MS = 16.667;

    std::mt19937 rng(42);

    for (int i = 0; i < FRAMES; ++i) {
        auto start = Clock::now();

        // 正常帧
        double workMs = TARGET_MS * 0.8;

        // 每 50 帧一次大卡顿 (模拟 GC 或 shader 编译)
        if (i % 50 == 0 && i > 0) {
            workMs = TARGET_MS * 3.0; // 3 帧的工作量
            std::cout << "    [Frame " << i << "] STUTTER! (simulated GC/shader compile)\n";
        }
        // 偶尔小卡顿 (模拟内存分配)
        else if (i % 15 == 0) {
            workMs = TARGET_MS * 1.5;
        }

        // 模拟工作
        while (Duration(Clock::now() - start).count() * 1000.0 < workMs) {
            // spin
        }

        double dt = Duration(Clock::now() - start).count() * 1000.0;
        analyzer.record(dt);
    }

    analyzer.analyze("Periodic Stutter");
    std::cout << "  注意 P99 和 Max 远高于 P50 -> 用户体验远比平均 FPS 差\n";
}

// ============================================================
// 场景3: 不稳定的工作负载
// ============================================================
void demo_variable_workload() {
    perf::printHeader("Variable Workload (some frames heavier than others)");

    FramePacingAnalyzer analyzer;
    constexpr int FRAMES = 200;

    std::mt19937 rng(42);
    std::normal_distribution<double> work_dist(10.0, 4.0); // 平均10ms,标准差4ms

    for (int i = 0; i < FRAMES; ++i) {
        auto start = Clock::now();

        double workMs = std::max(1.0, work_dist(rng));

        while (Duration(Clock::now() - start).count() * 1000.0 < workMs) {
            // spin
        }

        double dt = Duration(Clock::now() - start).count() * 1000.0;
        analyzer.record(dt);
    }

    analyzer.analyze("Variable Workload");
    std::cout << "  高 stddev 表示帧步调不稳定\n"
              << "  优化方法: 将重工作分摊到多帧 (amortized work)\n";
}

// ============================================================
// 场景4: 展示 sleep 精度问题
// ============================================================
void demo_sleep_precision() {
    perf::printHeader("Sleep Precision Problem on Windows");

    FramePacingAnalyzer analyzer_naive, analyzer_spin;
    constexpr int FRAMES = 100;
    constexpr double TARGET_MS = 16.667;

    // 方式1: 只用 Sleep (精度差)
    std::cout << "  Testing naive Sleep...\n";
    for (int i = 0; i < FRAMES; ++i) {
        auto start = Clock::now();

        // 模拟工作
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        // 用 sleep 等待剩余时间
        double elapsed = Duration(Clock::now() - start).count() * 1000.0;
        double remaining = TARGET_MS - elapsed;
        if (remaining > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(remaining)));
        }

        double dt = Duration(Clock::now() - start).count() * 1000.0;
        analyzer_naive.record(dt);
    }

    // 方式2: Sleep + spin-wait 混合
    std::cout << "  Testing Sleep + spin-wait...\n";
    for (int i = 0; i < FRAMES; ++i) {
        auto start = Clock::now();

        // 模拟工作
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        // 先 sleep 到接近目标
        double elapsed = Duration(Clock::now() - start).count() * 1000.0;
        double remaining = TARGET_MS - elapsed;
        if (remaining > 2.0) { // 留 2ms 给 spin-wait
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(remaining - 2)));
        }

        // spin-wait 精确等待
        while (Duration(Clock::now() - start).count() * 1000.0 < TARGET_MS) {
            // busy wait
        }

        double dt = Duration(Clock::now() - start).count() * 1000.0;
        analyzer_spin.record(dt);
    }

    analyzer_naive.analyze("Naive Sleep");
    analyzer_spin.analyze("Sleep + Spin-wait");

    std::cout << "  Sleep + Spin-wait 的 stddev 应该明显更低\n"
              << "  代价是 spin-wait 期间 CPU 会跑满\n";
}

int main() {
    perf::initConsole();
    std::cout << "===== 02: Frame Pacing Tutorial =====\n";
    std::cout << "Why FPS alone doesn't tell the full story\n";

#ifdef _WIN32
    // 提高 Windows timer 精度
    timeBeginPeriod(1);
    std::cout << "[Windows] timeBeginPeriod(1) called for better timer resolution\n";
#endif

    demo_ideal_pacing();
    demo_periodic_stutter();
    demo_variable_workload();
    demo_sleep_precision();

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    std::cout << "\n===== Key Takeaways =====\n"
              << "1. 平均 FPS 不能反映真实体验, 看 P99 帧时间\n"
              << "2. 1% 的卡顿帧就能让用户感觉不流畅\n"
              << "3. 帧步调 (帧间隔的一致性) 比纯帧率更重要\n"
              << "4. Windows Sleep 精度差, 需要 spin-wait 补偿\n"
              << "5. 工具: NVIDIA FrameView, PresentMon, GPU-Z\n";

    return 0;
}
