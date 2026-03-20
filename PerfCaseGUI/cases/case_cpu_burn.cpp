/*
 * case_cpu_burn.cpp - CPU 密集型计算负载
 *
 * 持续模式: 每帧执行可调节强度的 sin/cos 密集运算。
 * 用途: 模拟 CPU 密集型帧、触发 VTune Hotspot。
 */

#include "perf_case.h"
#include "tracy_integration.h"
#include "imgui.h"
#include <cmath>

class CpuBurnCase : public PerfCase {
public:
    const char* getCategory()    const override { return "CPU"; }
    const char* getName()        const override { return "CPU Burn"; }
    const char* getDescription() const override {
        return "Continuous sin/cos heavy computation.\n"
               "Adjust iterations to control CPU load per frame.\n"
               "Use VTune Hotspots or Tracy to observe this workload.";
    }

    void onUpdate(float) override {
        PERF_ZONE_SCOPED_N("CpuBurn");
        volatile double result = 0;
        for (int i = 0; i < iterations_; i++) {
            result += std::sin(static_cast<double>(i) * 0.001) *
                      std::cos(static_cast<double>(i) * 0.002);
        }
    }

    void onDrawUI() override {
        ImGui::SliderInt("Iterations", &iterations_, 100000, 50000000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        ImGui::Text("Approx cost: %.2f ms/frame", lastUpdateMs);
    }

private:
    int iterations_ = 1000000;
};

ADD_PERF_CASE(CpuBurnCase)
