/*
 * case_cache_miss.cpp - CPU 缓存命中率测试
 *
 * 基准模式: 对比顺序 vs 随机内存访问的性能差异。
 * 可调数组大小，直观演示 L1/L2/L3 缓存的行为。
 */

#include "perf_case.h"
#include "imgui.h"
#include <vector>
#include <random>
#include <cstring>
#include <algorithm>
#include <numeric>

class CacheMissCase : public PerfCase {
public:
    const char* getCategory()    const override { return "Stutter"; }
    const char* getName()        const override { return "Cache Miss"; }
    const char* getDescription() const override {
        return "Compare sequential vs random memory access.\n"
               "Shows L1/L2/L3 cache effects. Increase array size\n"
               "to exceed cache levels and observe performance cliff.";
    }

    void onActivate() override {
        rebuildData();
    }

    void onDeactivate() override {
        data_.clear();
        data_.shrink_to_fit();
        indices_.clear();
        indices_.shrink_to_fit();
    }

    void onUpdate(float) override {
        // Continuous mode: run selected access pattern every frame
        if (continuous_) {
            if (randomAccess_)
                doRandomAccess();
            else
                doSequentialAccess();
        }
    }

    void onDrawUI() override {
        bool sizeChanged = ImGui::SliderInt("Array Size (MB)", &arraySizeMB_, 1, 128);
        if (sizeChanged) rebuildData();

        ImGui::Checkbox("Continuous (per-frame)", &continuous_);
        ImGui::Checkbox("Random Access", &randomAccess_);

        ImGui::Separator();

        if (ImGui::Button("Run Benchmark")) {
            runBenchmark();
        }

        if (benchmarkDone_) {
            ImGui::Spacing();
            ImGui::Text("--- Results (array = %d MB) ---", benchArrayMB_);
            ImGui::Text("Sequential: %.2f ms", seqMs_);
            ImGui::Text("Random:     %.2f ms", rndMs_);
            float ratio = seqMs_ > 0 ? rndMs_ / seqMs_ : 0;
            ImGui::Text("Random / Sequential = %.1fx slower", ratio);

            // 简易柱状图
            float values[] = { seqMs_, rndMs_ };
            ImGui::PlotHistogram("##cmp", values, 2, 0, "Seq vs Rnd",
                                 0, rndMs_ * 1.2f, ImVec2(200, 80));
        }
    }

private:
    int  arraySizeMB_ = 4;
    bool continuous_  = false;
    bool randomAccess_ = true;

    std::vector<uint8_t> data_;
    std::vector<size_t>  indices_;

    bool  benchmarkDone_ = false;
    int   benchArrayMB_  = 0;
    float seqMs_ = 0, rndMs_ = 0;

    void rebuildData() {
        size_t size = static_cast<size_t>(arraySizeMB_) * 1024 * 1024;
        data_.resize(size);
        std::memset(data_.data(), 0xAB, size);

        // 预生成随机索引
        indices_.resize(500000);
        std::mt19937 rng(42);
        std::uniform_int_distribution<size_t> dist(0, size - 1);
        for (auto& idx : indices_) idx = dist(rng);
    }

    void doSequentialAccess() {
        volatile uint64_t sum = 0;
        size_t size = data_.size();
        for (size_t i = 0; i < size && i < 500000; i += 64)
            sum += data_[i];
    }

    void doRandomAccess() {
        volatile uint64_t sum = 0;
        for (size_t idx : indices_)
            sum += data_[idx];
    }

    void runBenchmark() {
        rebuildData();
        benchArrayMB_ = arraySizeMB_;

        // 多次运行取平均
        constexpr int RUNS = 5;
        float seqTotal = 0, rndTotal = 0;
        for (int r = 0; r < RUNS; r++) {
            seqTotal += measure([this]() { doSequentialAccess(); });
            rndTotal += measure([this]() { doRandomAccess(); });
        }
        seqMs_ = seqTotal / RUNS;
        rndMs_ = rndTotal / RUNS;
        benchmarkDone_ = true;
    }
};

ADD_PERF_CASE(CacheMissCase)
