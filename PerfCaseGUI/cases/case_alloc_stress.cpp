/*
 * case_alloc_stress.cpp - 内存分配压力测试
 *
 * 持续模式: 每帧执行大量 malloc/free，对比对象池。
 * 用途: 演示堆分配开销，以及对象池如何消除分配卡顿。
 */

#include "perf_case.h"
#include "tracy_integration.h"
#include "imgui.h"
#include <vector>
#include <cstring>
#include <cstdlib>

class AllocStressCase : public PerfCase {
public:
    const char* getCategory()    const override { return "Memory"; }
    const char* getName()        const override { return "Alloc Stress"; }
    const char* getDescription() const override {
        return "Per-frame malloc/free vs object pool.\n"
               "Frequent allocation causes heap fragmentation\n"
               "and unpredictable latency spikes.";
    }

    void onDeactivate() override {
        pool_.clear();
    }

    void onUpdate(float) override {
        PERF_ZONE_SCOPED_N("AllocStress");
        std::vector<void*> ptrs;
        ptrs.reserve(allocCount_);

        if (usePool_) {
            pool_.ensureCapacity(allocSize_, allocCount_);
            for (int i = 0; i < allocCount_; i++) {
                void* p = pool_.acquire();
                std::memset(p, 0, allocSize_);
                ptrs.push_back(p);
            }
            for (void* p : ptrs) pool_.release(p);
        } else {
            for (int i = 0; i < allocCount_; i++) {
                void* p = std::malloc(allocSize_);
                std::memset(p, 0, allocSize_);
                ptrs.push_back(p);
            }
            for (void* p : ptrs) std::free(p);
        }
    }

    void onDrawUI() override {
        ImGui::SliderInt("Count", &allocCount_, 100, 100000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        ImGui::SliderInt("Size (bytes)", &allocSize_, 16, 65536, "%d",
                         ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("Use Object Pool", &usePool_);

        if (active) {
            ImGui::Text("Cost: %.2f ms/frame", lastUpdateMs);
        }

        ImGui::Separator();
        if (ImGui::Button("Run Comparison")) {
            runComparison();
        }
        if (compDone_) {
            ImGui::Text("Heap:  %.2f ms", heapMs_);
            ImGui::Text("Pool:  %.2f ms", poolMs_);
            float ratio = poolMs_ > 0 ? heapMs_ / poolMs_ : 0;
            ImGui::TextColored(ImVec4(0.2f, 1, 0.2f, 1),
                               "Pool speedup: %.1fx", ratio);
        }
    }

private:
    int  allocCount_ = 5000;
    int  allocSize_  = 256;
    bool usePool_    = false;

    bool  compDone_ = false;
    float heapMs_ = 0, poolMs_ = 0;

    // Simple pool
    struct Pool {
        std::vector<void*> freeList;
        std::vector<void*> allBlocks;
        size_t blockSize = 0;

        void ensureCapacity(size_t sz, int count) {
            if (blockSize != sz) clear();
            blockSize = sz;
            while (static_cast<int>(freeList.size()) < count) {
                void* p = std::malloc(sz);
                freeList.push_back(p);
                allBlocks.push_back(p);
            }
        }

        void* acquire() {
            if (freeList.empty()) {
                void* p = std::malloc(blockSize);
                allBlocks.push_back(p);
                return p;
            }
            void* p = freeList.back();
            freeList.pop_back();
            return p;
        }

        void release(void* p) { freeList.push_back(p); }

        void clear() {
            for (void* p : allBlocks) std::free(p);
            allBlocks.clear();
            freeList.clear();
            blockSize = 0;
        }

        ~Pool() { clear(); }
    } pool_;

    void runComparison() {
        constexpr int RUNS = 5;
        float h = 0, p = 0;
        for (int r = 0; r < RUNS; r++) {
            h += measure([&]() {
                std::vector<void*> ptrs;
                ptrs.reserve(allocCount_);
                for (int i = 0; i < allocCount_; i++) {
                    void* ptr = std::malloc(allocSize_);
                    std::memset(ptr, 0, allocSize_);
                    ptrs.push_back(ptr);
                }
                for (void* ptr : ptrs) std::free(ptr);
            });

            Pool tmpPool;
            tmpPool.ensureCapacity(allocSize_, allocCount_);
            p += measure([&]() {
                std::vector<void*> ptrs;
                ptrs.reserve(allocCount_);
                for (int i = 0; i < allocCount_; i++) {
                    void* ptr = tmpPool.acquire();
                    std::memset(ptr, 0, allocSize_);
                    ptrs.push_back(ptr);
                }
                for (void* ptr : ptrs) tmpPool.release(ptr);
            });
        }
        heapMs_ = h / RUNS;
        poolMs_ = p / RUNS;
        compDone_ = true;
    }
};

ADD_PERF_CASE(AllocStressCase)
