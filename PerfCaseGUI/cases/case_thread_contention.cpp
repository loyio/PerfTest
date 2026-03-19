/*
 * case_thread_contention.cpp - 多线程争用持续负载
 *
 * 持续模式: 每帧在多线程下争用一把锁。
 * 用于持续观察锁争用对帧时间的影响。
 * 可配合 Tracy/VTune Threading 分析。
 */

#include "perf_case.h"
#include "imgui.h"
#include <vector>
#include <thread>
#include <mutex>

class ThreadContentionCase : public PerfCase {
public:
    const char* getCategory()    const override { return "CPU"; }
    const char* getName()        const override { return "Thread Contention"; }
    const char* getDescription() const override {
        return "Per-frame multi-threaded mutex contention.\n"
               "Creates threads every frame that fight over a lock.\n"
               "Use Tracy or VTune Threading to see wait times.";
    }

    void onUpdate(float) override {
        volatile int64_t counter = 0;
        std::mutex mtx;
        int opsPerThread = ops_ / threads_;

        std::vector<std::thread> threads;
        for (int t = 0; t < threads_; t++) {
            threads.emplace_back([&]() {
                for (int i = 0; i < opsPerThread; i++) {
                    std::lock_guard<std::mutex> lock(mtx);
                    counter++;
                }
            });
        }
        for (auto& t : threads) t.join();
    }

    void onDrawUI() override {
        ImGui::SliderInt("Threads", &threads_, 2, 16);
        ImGui::SliderInt("Total Ops", &ops_, 10000, 2000000, "%d",
                         ImGuiSliderFlags_Logarithmic);

        if (active) {
            ImGui::Text("Cost: %.2f ms/frame", lastUpdateMs);
        }
    }

private:
    int threads_ = 4;
    int ops_     = 100000;
};

ADD_PERF_CASE(ThreadContentionCase)
