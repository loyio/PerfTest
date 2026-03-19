/*
 * case_false_sharing.cpp - 伪共享 (False Sharing) 测试
 *
 * 基准模式: 对比非对齐 vs 缓存行对齐的多线程计数器性能。
 * 核心问题: 多个线程写相邻地址导致缓存行反复失效。
 */

#include "perf_case.h"
#include "imgui.h"
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>

static constexpr size_t CACHE_LINE = 64;

class FalseSharingCase : public PerfCase {
public:
    const char* getCategory()    const override { return "Stutter"; }
    const char* getName()        const override { return "False Sharing"; }
    const char* getDescription() const override {
        return "Multi-threaded counter: packed vs cache-line aligned.\n"
               "False sharing causes ~3-10x slowdown when threads\n"
               "write to adjacent memory (same cache line).";
    }

    void onDrawUI() override {
        ImGui::SliderInt("Threads", &threadCount_, 2, 16);
        ImGui::SliderInt("Ops/thread (K)", &opsK_, 100, 10000);

        if (ImGui::Button("Run Benchmark")) {
            runBenchmark();
        }

        if (done_) {
            ImGui::Spacing();
            ImGui::Text("--- Results (%d threads, %dK ops each) ---",
                        benchThreads_, benchOpsK_);
            ImGui::Text("Packed (false sharing):  %.2f ms", packedMs_);
            ImGui::Text("Aligned (no sharing):    %.2f ms", alignedMs_);
            float ratio = alignedMs_ > 0 ? packedMs_ / alignedMs_ : 0;
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1),
                               "False sharing penalty: %.1fx", ratio);

            float v[] = { packedMs_, alignedMs_ };
            ImGui::PlotHistogram("##fs", v, 2, 0, "Packed vs Aligned",
                                 0, packedMs_ * 1.2f, ImVec2(200, 80));
        }
    }

private:
    int threadCount_ = 4;
    int opsK_ = 1000;

    bool  done_ = false;
    int   benchThreads_ = 0, benchOpsK_ = 0;
    float packedMs_ = 0, alignedMs_ = 0;

    struct PackedCounters {
        int64_t counters[16]; // 紧密排列，同一缓存行
    };

    struct alignas(CACHE_LINE) AlignedCounter {
        int64_t value;
        char padding[CACHE_LINE - sizeof(int64_t)];
    };

    void runBenchmark() {
        int nThreads = threadCount_;
        int ops = opsK_ * 1000;
        benchThreads_ = nThreads;
        benchOpsK_ = opsK_;

        // Packed
        constexpr int RUNS = 3;
        float packedTotal = 0, alignedTotal = 0;

        for (int r = 0; r < RUNS; r++) {
            // Packed
            packedTotal += measure([&]() {
                PackedCounters pc = {};
                std::vector<std::thread> threads;
                for (int t = 0; t < nThreads; t++) {
                    threads.emplace_back([&pc, t, ops]() {
                        for (int i = 0; i < ops; i++)
                            pc.counters[t]++;
                    });
                }
                for (auto& th : threads) th.join();
            });

            // Aligned
            alignedTotal += measure([&]() {
                std::vector<AlignedCounter> ac(nThreads);
                std::vector<std::thread> threads;
                for (int t = 0; t < nThreads; t++) {
                    threads.emplace_back([&ac, t, ops]() {
                        for (int i = 0; i < ops; i++)
                            ac[t].value++;
                    });
                }
                for (auto& th : threads) th.join();
            });
        }

        packedMs_  = packedTotal / RUNS;
        alignedMs_ = alignedTotal / RUNS;
        done_ = true;
    }
};

ADD_PERF_CASE(FalseSharingCase)
