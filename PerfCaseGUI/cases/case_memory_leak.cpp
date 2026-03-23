/*
 * case_memory_leak.cpp - 内存泄漏模拟
 *
 * 持续模式: 每帧泄漏一定量内存，用于练习工具检测。
 * 可在 VTune Memory / VS Diagnostic Tools / VMMap 中观察增长。
 */

#include "perf_case.h"
#include "tracy_integration.h"
#include "imgui.h"
#include <vector>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

class MemoryLeakCase : public PerfCase {
public:
    const char* getCategory()    const override { return "Memory"; }
    const char* getName()        const override { return "Memory Leak"; }
    const char* getDescription() const override {
        return "Intentional memory leak simulator.\n"
               "Watch working set grow in Task Manager, VMMap,\n"
               "or VS Diagnostic Tools. Click 'Free All' to recover.";
    }

    void onDeactivate() override {
        freeAll();
    }

    void onUpdate(float) override {
        ZoneScopedN("MemoryLeak");
        if (!leaking_) return;

        for (int i = 0; i < leaksPerFrame_; i++) {
            void* p = std::malloc(leakSizeBytes_);
            std::memset(p, 0xCD, leakSizeBytes_);
            leaked_.push_back(p);
        }
        totalLeakedBytes_ += static_cast<size_t>(leaksPerFrame_) * leakSizeBytes_;
        TracyPlot("LeakedMB", totalLeakedBytes_ / (1024.0 * 1024.0));
    }

    void onDrawUI() override {
        ImGui::Checkbox("Leak Every Frame", &leaking_);
        ImGui::SliderInt("Leaks/frame", &leaksPerFrame_, 1, 1000);
        ImGui::SliderInt("Leak size (bytes)", &leakSizeBytes_, 64, 65536, "%d",
                         ImGuiSliderFlags_Logarithmic);

        ImGui::Separator();
        ImGui::Text("Leaked blocks: %llu", (unsigned long long)leaked_.size());

        double mb = totalLeakedBytes_ / (1024.0 * 1024.0);
        ImGui::Text("Leaked total: %.2f MB", mb);

#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            ImGui::Text("Working Set: %.1f MB",
                        pmc.WorkingSetSize / (1024.0 * 1024.0));
            ImGui::Text("Pagefile:    %.1f MB",
                        pmc.PagefileUsage / (1024.0 * 1024.0));
        }
#endif

        ImGui::Separator();
        if (ImGui::Button("Free All Leaked Memory")) {
            freeAll();
        }
    }

private:
    bool leaking_       = true;
    int  leaksPerFrame_ = 10;
    int  leakSizeBytes_ = 1024;

    std::vector<void*> leaked_;
    size_t totalLeakedBytes_ = 0;

    void freeAll() {
        ZoneScopedN("MemoryLeak_FreeAll");
        for (void* p : leaked_) std::free(p);
        leaked_.clear();
        totalLeakedBytes_ = 0;
    }
};

ADD_PERF_CASE(MemoryLeakCase)
