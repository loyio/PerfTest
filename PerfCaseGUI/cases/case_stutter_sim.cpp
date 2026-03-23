/*
 * case_stutter_sim.cpp - 随机卡顿模拟
 *
 * 持续模式: 按概率注入 Sleep 延迟，模拟不定期的帧卡顿。
 * 用于练习 WPR/WPA、Tracy 等工具的卡顿定位能力。
 */

#include "perf_case.h"
#include "tracy_integration.h"
#include "imgui.h"
#include <random>
#include <thread>
#include <chrono>
#include <cmath>

class StutterSimCase : public PerfCase {
public:
    const char* getCategory()    const override { return "Framerate"; }
    const char* getName()        const override { return "Stutter Simulator"; }
    const char* getDescription() const override {
        return "Randomly inject frame delays to simulate stutter.\n"
               "Watch the frame time graph spike.\n"
               "Practice finding the cause with WPR/WPA or Tracy.";
    }

    void onUpdate(float) override {
        ZoneScopedN("StutterSim");
        std::uniform_int_distribution<int> dist(1, 100);
        if (dist(rng_) <= chancePct_) {
            if (useBusyWait_) {
                // Busy-wait: shows as CPU burn in profiler
                auto end = std::chrono::high_resolution_clock::now()
                         + std::chrono::milliseconds(durationMs_);
                volatile double x = 0;
                while (std::chrono::high_resolution_clock::now() < end)
                    x += std::sin(x + 1.0);
            } else {
                // Sleep: shows as thread waiting in profiler
                std::this_thread::sleep_for(std::chrono::milliseconds(durationMs_));
            }
            stutterCount_++;
        }
        totalFrames_++;
    }

    void onActivate() override {
        stutterCount_ = 0;
        totalFrames_  = 0;
    }

    void onDrawUI() override {
        ImGui::SliderInt("Chance (%%)", &chancePct_, 1, 50);
        ImGui::SliderInt("Duration (ms)", &durationMs_, 5, 200);
        ImGui::Checkbox("Busy-wait (shows as CPU)", &useBusyWait_);

        ImGui::Separator();
        ImGui::Text("Stutters: %d / %d frames (%.1f%%)",
                    stutterCount_, totalFrames_,
                    totalFrames_ > 0 ? 100.0f * stutterCount_ / totalFrames_ : 0.0f);
    }

private:
    int  chancePct_   = 5;
    int  durationMs_  = 50;
    bool useBusyWait_ = false;

    std::mt19937 rng_{std::random_device{}()};
    int stutterCount_ = 0;
    int totalFrames_  = 0;
};

ADD_PERF_CASE(StutterSimCase)
