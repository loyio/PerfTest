/*
 * case_lock_contention.cpp - 锁竞争测试
 *
 * 基准模式: 对比粗粒度锁 vs 分片锁 vs 无锁的多线程性能。
 */

#include "perf_case.h"
#include "tracy_integration.h"
#include "imgui.h"
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>

class LockContentionCase : public PerfCase {
public:
    const char* getCategory()    const override { return "Stutter"; }
    const char* getName()        const override { return "Lock Contention"; }
    const char* getDescription() const override {
        return "Compare coarse lock vs sharded lock vs atomic.\n"
               "Shows how lock granularity affects throughput\n"
               "under heavy multi-threaded contention.";
    }

    void onDrawUI() override {
        ImGui::SliderInt("Threads", &threadCount_, 2, 16);
        ImGui::SliderInt("Ops/thread (K)", &opsK_, 10, 1000);

        if (ImGui::Button("Run Benchmark")) {
            runBenchmark();
        }

        if (done_) {
            ImGui::Spacing();
            ImGui::Text("--- Results (%d threads, %dK ops) ---",
                        benchThreads_, benchOpsK_);
            ImGui::Text("Single mutex:   %.2f ms", singleMs_);
            ImGui::Text("Sharded (16x):  %.2f ms", shardedMs_);
            ImGui::Text("Atomic:         %.2f ms", atomicMs_);

            float v[] = { singleMs_, shardedMs_, atomicMs_ };
            const char* labels[] = { "Single", "Sharded", "Atomic" };
            float maxV = *std::max_element(v, v + 3);
            ImGui::PlotHistogram("##lc", v, 3, 0, nullptr,
                                 0, maxV * 1.2f, ImVec2(250, 80));
            for (int i = 0; i < 3; i++) {
                ImGui::SameLine();
            }
        }
    }

private:
    int threadCount_ = 4;
    int opsK_ = 200;

    bool  done_ = false;
    int   benchThreads_ = 0, benchOpsK_ = 0;
    float singleMs_ = 0, shardedMs_ = 0, atomicMs_ = 0;

    void runBenchmark() {
        ZoneScopedN("LockContention_Bench");
        int nThreads = threadCount_;
        int ops = opsK_ * 1000;
        benchThreads_ = nThreads;
        benchOpsK_ = opsK_;

        constexpr int RUNS = 3;
        float s1 = 0, s2 = 0, s3 = 0;

        for (int r = 0; r < RUNS; r++) {
            // Single mutex
            s1 += measure([&]() {
                std::mutex mtx;
                volatile int64_t counter = 0;
                std::vector<std::thread> threads;
                for (int t = 0; t < nThreads; t++) {
                    threads.emplace_back([&]() {
                        for (int i = 0; i < ops; i++) {
                            std::lock_guard<std::mutex> lock(mtx);
                            counter++;
                        }
                    });
                }
                for (auto& th : threads) th.join();
            });

            // Sharded mutex
            s2 += measure([&]() {
                constexpr int SHARDS = 16;
                std::mutex mtxs[SHARDS];
                int64_t counters[SHARDS] = {};
                std::vector<std::thread> threads;
                for (int t = 0; t < nThreads; t++) {
                    threads.emplace_back([&, t]() {
                        int shard = t % SHARDS;
                        for (int i = 0; i < ops; i++) {
                            std::lock_guard<std::mutex> lock(mtxs[shard]);
                            counters[shard]++;
                        }
                    });
                }
                for (auto& th : threads) th.join();
            });

            // Atomic
            s3 += measure([&]() {
                std::atomic<int64_t> counter{0};
                std::vector<std::thread> threads;
                for (int t = 0; t < nThreads; t++) {
                    threads.emplace_back([&]() {
                        for (int i = 0; i < ops; i++)
                            counter.fetch_add(1, std::memory_order_relaxed);
                    });
                }
                for (auto& th : threads) th.join();
            });
        }

        singleMs_  = s1 / RUNS;
        shardedMs_ = s2 / RUNS;
        atomicMs_  = s3 / RUNS;
        done_ = true;
    }
};

ADD_PERF_CASE(LockContentionCase)
