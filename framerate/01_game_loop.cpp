/*
 * 01_game_loop.cpp - 游戏循环与帧率控制
 *
 * 学习要点:
 * 1. 固定时间步长 (Fixed timestep) - 物理模拟稳定性
 * 2. 可变时间步长 (Variable timestep) - 跟随显示器刷新
 * 3. 半固定步长 + 插值 (Fix your timestep!) - 最佳实践
 * 4. 帧率计算和统计
 *
 * 参考: Glenn Fiedler 的 "Fix Your Timestep!" 经典文章
 */

#include "profiler.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double>;
using TimePoint = Clock::time_point;

// ============================================================
// 帧率统计器
// ============================================================
class FrameStats {
public:
    void recordFrame(double dt) {
        frameTimes_.push_back(dt);
        if (frameTimes_.size() > 300) {
            frameTimes_.erase(frameTimes_.begin());
        }
    }

    void print() const {
        if (frameTimes_.empty()) return;

        double sum = std::accumulate(frameTimes_.begin(), frameTimes_.end(), 0.0);
        double avg = sum / frameTimes_.size();
        double minDt = *std::min_element(frameTimes_.begin(), frameTimes_.end());
        double maxDt = *std::max_element(frameTimes_.begin(), frameTimes_.end());

        std::cout << "  Frame Stats (last " << frameTimes_.size() << " frames):\n"
                  << "    Avg FPS:  " << (avg > 0 ? 1.0 / avg : 0) << "\n"
                  << "    Avg dt:   " << avg * 1000 << " ms\n"
                  << "    Min dt:   " << minDt * 1000 << " ms\n"
                  << "    Max dt:   " << maxDt * 1000 << " ms\n"
                  << "    Jitter:   " << (maxDt - minDt) * 1000 << " ms\n";
    }

private:
    std::vector<double> frameTimes_;
};

// ============================================================
// 模拟的游戏状态
// ============================================================
struct GameState {
    double posX = 0, posY = 0;
    double velX = 100.0, velY = 50.0; // pixels/sec
    int updateCount = 0;

    void update(double dt) {
        posX += velX * dt;
        posY += velY * dt;

        // 边界反弹 (800x600 窗口)
        if (posX < 0 || posX > 800) velX = -velX;
        if (posY < 0 || posY > 600) velY = -velY;
        posX = std::clamp(posX, 0.0, 800.0);
        posY = std::clamp(posY, 0.0, 600.0);

        updateCount++;
    }
};

// 模拟渲染 (用 sleep 代替)
void simulateRender(double /*alpha*/) {
    std::this_thread::sleep_for(std::chrono::microseconds(500)); // 模拟渲染耗时
}

// 模拟不稳定的工作负载 (制造帧率波动)
void simulateUnstableWork(int frame) {
    if (frame % 30 == 0) {
        // 每30帧来一次 "卡顿"
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ============================================================
// 方案1: 简单可变时间步长
// ============================================================
void demo_variable_timestep() {
    perf::printHeader("Variable Timestep (Simple but problematic)");
    std::cout << "  物理更新使用实际 dt, 帧率不稳定时物理也不稳\n\n";

    GameState state;
    FrameStats stats;
    auto lastTime = Clock::now();
    constexpr int FRAMES = 120;

    for (int frame = 0; frame < FRAMES; ++frame) {
        auto now = Clock::now();
        double dt = Duration(now - lastTime).count();
        lastTime = now;

        if (dt > 0.1) dt = 0.1; // 防止极端 dt

        // 用实际 dt 更新
        state.update(dt);
        simulateUnstableWork(frame);
        simulateRender(1.0);

        stats.recordFrame(dt);
    }

    std::cout << "  Final position: (" << state.posX << ", " << state.posY << ")\n";
    std::cout << "  Update count: " << state.updateCount << "\n";
    stats.print();
    std::cout << "\n  问题: 不同帧率下物理结果不同 (不可重现)\n";
}

// ============================================================
// 方案2: 固定时间步长
// ============================================================
void demo_fixed_timestep() {
    perf::printHeader("Fixed Timestep (Deterministic physics)");
    std::cout << "  物理固定 60Hz 更新, 渲染尽可能快\n\n";

    constexpr double FIXED_DT = 1.0 / 60.0; // 60Hz 物理

    GameState state;
    FrameStats stats;
    double accumulator = 0;
    auto lastTime = Clock::now();
    constexpr int FRAMES = 120;

    for (int frame = 0; frame < FRAMES; ++frame) {
        auto now = Clock::now();
        double frameDt = Duration(now - lastTime).count();
        lastTime = now;

        if (frameDt > 0.1) frameDt = 0.1;
        accumulator += frameDt;

        // 以固定步长消耗累积的时间
        while (accumulator >= FIXED_DT) {
            state.update(FIXED_DT);
            accumulator -= FIXED_DT;
        }

        simulateUnstableWork(frame);
        simulateRender(1.0);

        stats.recordFrame(frameDt);
    }

    std::cout << "  Final position: (" << state.posX << ", " << state.posY << ")\n";
    std::cout << "  Update count: " << state.updateCount << "\n";
    stats.print();
    std::cout << "\n  优点: 物理结果确定性, 每次运行结果相同\n"
              << "  缺点: 渲染可能显示物理步之间的位置, 导致抖动\n";
}

// ============================================================
// 方案3: 固定步长 + 插值 (最佳实践)
// ============================================================
void demo_fixed_timestep_interpolated() {
    perf::printHeader("Fixed Timestep + Interpolation (Best Practice)");
    std::cout << "  物理固定步长 + 渲染时在两帧间插值, 丝滑!\n\n";

    constexpr double FIXED_DT = 1.0 / 60.0;

    GameState currentState, previousState;
    FrameStats stats;
    double accumulator = 0;
    auto lastTime = Clock::now();
    constexpr int FRAMES = 120;

    for (int frame = 0; frame < FRAMES; ++frame) {
        auto now = Clock::now();
        double frameDt = Duration(now - lastTime).count();
        lastTime = now;

        if (frameDt > 0.1) frameDt = 0.1;
        accumulator += frameDt;

        while (accumulator >= FIXED_DT) {
            previousState = currentState;
            currentState.update(FIXED_DT);
            accumulator -= FIXED_DT;
        }

        // alpha = 当前帧在两次物理步之间的插值比例
        double alpha = accumulator / FIXED_DT;

        // 渲染使用插值位置 (而不是直接用物理位置)
        double renderX = previousState.posX * (1.0 - alpha) + currentState.posX * alpha;
        double renderY = previousState.posY * (1.0 - alpha) + currentState.posY * alpha;

        simulateUnstableWork(frame);
        simulateRender(alpha);

        stats.recordFrame(frameDt);

        if (frame == FRAMES - 1) {
            std::cout << "  Render position: (" << renderX << ", " << renderY << ")\n";
            std::cout << "  Alpha: " << alpha << "\n";
        }
    }

    std::cout << "  Physics position: (" << currentState.posX << ", " << currentState.posY << ")\n";
    std::cout << "  Update count: " << currentState.updateCount << "\n";
    stats.print();
    std::cout << "\n  这是大多数高质量游戏引擎使用的方案\n";
}

// ============================================================
// 帧率限制器 (Frame Limiter)
// ============================================================
void demo_frame_limiter() {
    perf::printHeader("Frame Limiter (Target 60 FPS)");

    constexpr double TARGET_FPS = 60.0;
    constexpr double TARGET_FRAME_TIME = 1.0 / TARGET_FPS;

    FrameStats stats;
    constexpr int FRAMES = 120;

    for (int frame = 0; frame < FRAMES; ++frame) {
        auto frameStart = Clock::now();

        // 模拟游戏工作
        simulateUnstableWork(frame);
        simulateRender(1.0);

        // 计算剩余时间并 sleep
        auto frameEnd = Clock::now();
        double elapsed = Duration(frameEnd - frameStart).count();
        double sleepTime = TARGET_FRAME_TIME - elapsed;

        if (sleepTime > 0) {
            // 注意: std::this_thread::sleep_for 精度有限 (~1-15ms on Windows)
            // 对于精确帧率控制，需要 spin-wait 来补偿
            auto sleepDuration = std::chrono::duration<double>(sleepTime * 0.9);
            std::this_thread::sleep_for(
                std::chrono::duration_cast<std::chrono::microseconds>(sleepDuration));

            // Spin-wait 精确等待剩余时间
            while (Duration(Clock::now() - frameStart).count() < TARGET_FRAME_TIME) {
                // busy wait
            }
        }

        auto actualEnd = Clock::now();
        double dt = Duration(actualEnd - frameStart).count();
        stats.recordFrame(dt);
    }

    stats.print();
    std::cout << "\n  注意: Windows 默认 timer 精度 ~15ms\n"
              << "  精确帧率限制需要 spin-wait 或 timeBeginPeriod(1)\n";
}

int main() {
    perf::initConsole();
    std::cout << "===== 01: Game Loop & Frame Rate Tutorial =====\n";
    std::cout << "Compare different game loop strategies\n";

    demo_variable_timestep();
    demo_fixed_timestep();
    demo_fixed_timestep_interpolated();
    demo_frame_limiter();

    std::cout << "\n===== Key Takeaways =====\n"
              << "1. 可变时间步长简单但物理不可重现\n"
              << "2. 固定时间步长保证确定性物理模拟\n"
              << "3. 固定步长 + 渲染插值 = 最佳实践\n"
              << "4. 帧率限制需要 sleep + spin-wait 组合\n"
              << "5. Windows: timeBeginPeriod(1) 可提高 timer 精度\n";

    return 0;
}
