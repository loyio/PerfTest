/*
 * case_telemetry_demo.cpp - 生产 Telemetry 系统交互式演示
 *
 * 模拟多线程游戏引擎场景:
 *   - 主线程: 游戏逻辑 + 物理 + 动画
 *   - IO 线程: 资源加载
 *   - 渲染线程: 绘制提交
 *   - 共享锁: 模拟线程间的竞争
 *
 * 交互功能:
 *   - [Simulate Stutter]   手动触发卡顿 (可选严重程度)
 *   - [Simulate Memory Leak] 持续泄漏内存, Telemetry 记录
 *   - [Heavy IO Load]      模拟大量 IO 加载
 *   - [Lock Storm]         大量锁竞争
 *   - [GC Pause]           模拟 GC 暂停
 *
 * 查看方法:
 *   1. 浏览器打开 https://ui.perfetto.dev
 *   2. 把 telemetry_output/ 下的 .json 文件拖入浏览器
 *
 * 需要: cmake -DPERF_ENABLE_TELEMETRY=ON ..
 */

#include "perf_case.h"
#include "tracy_integration.h"
#include "telemetry.h"
#include "imgui.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

// 模拟资源路径
static const char* kAssetPaths[] = {
    "characters/hero_skin_03.fbx",
    "effects/fire_burst_01.vfx",
    "textures/terrain_diffuse_4k.dds",
    "animations/idle_combat.anim",
    "audio/bgm_battle_01.ogg",
    "maps/dungeon_boss_arena.level",
    "shaders/pbr_standard.hlsl",
    "ui/main_menu_atlas.png",
};
static const int kAssetPathCount = sizeof(kAssetPaths) / sizeof(kAssetPaths[0]);

class TelemetryDemoCase : public PerfCase {
public:
    const char* getCategory()    const override { return "Telemetry"; }
    const char* getName()        const override { return "Production Telemetry"; }
    const char* getDescription() const override {
        return
            "Interactive telemetry demo with scenario buttons.\n"
            "Simulates Main / IO / Render threads.\n"
            "Click buttons to trigger stutter, memory leak, etc.\n\n"
            "View: drag .json from telemetry_output/ into\n"
            "https://ui.perfetto.dev";
    }

    void onActivate() override {
        telemetry::Config cfg;
        cfg.stutterThresholdMs    = stutterThreshold_;
        cfg.outputDir             = "./telemetry_output";
        cfg.autoExport            = true;
        cfg.drainIntervalMs       = 500.0f;
        cfg.autoExportIntervalSec = autoExportSec_;
        cfg.maxTraceFiles         = maxTraceFiles_;
        cfg.uploadBudgetBytes     = uploadBudgetMB_ * 1024ULL * 1024ULL;
        cfg.stutterOnly           = stutterOnly_;
        cfg.stutterCaptureBeforeMs = stutterCaptureBefore_;
        cfg.stutterCaptureAfterMs  = stutterCaptureAfter_;
        telemetry::Manager::get().init(cfg);

        TracySetThreadName("Main Thread");

        running_ = true;
        frameCounter_ = 0;
        assetIndex_ = 0;
        exported_ = false;

        // 重置场景状态
        triggerStutter_ = false;
        stutterMs_ = 100;
        leaking_ = false;
        triggerHeavyIO_ = false;
        triggerLockStorm_ = false;
        triggerGCPause_ = false;
        totalLeakedBytes_ = 0;
        for (auto* p : leakedBlocks_) std::free(p);
        leakedBlocks_.clear();

        // IO Thread
        ioThread_ = std::thread([this]() {
            TracySetThreadName("IO Thread");
            int loadIdx = 0;
            while (running_) {
                const char* asset = kAssetPaths[loadIdx % kAssetPathCount];
                {
                    TracyScopedContext(asset);
                    ZoneScopedN("IO::LoadAsset");
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(ioLoadMs_));
                }

                // 按钮触发: 重度 IO 模拟 (一次加载很多资源)
                if (triggerHeavyIO_.load(std::memory_order_relaxed)) {
                    triggerHeavyIO_.store(false, std::memory_order_relaxed);
                    for (int i = 0; i < 8; i++) {
                        const char* heavyAsset = kAssetPaths[i % kAssetPathCount];
                        TracyScopedContext(heavyAsset);
                        ZoneScopedN("IO::HeavyLoad");
                        TracyMessageL("Heavy IO burst triggered");
                        // 模拟读取大文件
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(30 + (i * 10)));
                        {
                            std::lock_guard<decltype(sharedMutex_)> lock(sharedMutex_);
                            ZoneScopedN("IO::UpdateCache");
                            std::this_thread::sleep_for(std::chrono::milliseconds(3));
                        }
                    }
                }

                {
                    std::lock_guard<decltype(sharedMutex_)> lock(sharedMutex_);
                    ZoneScopedN("IO::UpdateCache");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                loadIdx++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        // Render Thread
        renderThread_ = std::thread([this]() {
            TracySetThreadName("Render Thread");
            while (running_) {
                {
                    TracyScopedContext("DrawScene: particle_fire_01");
                    ZoneScopedN("Render::DrawScene");
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(renderDrawMs_));
                }
                {
                    std::lock_guard<decltype(sharedMutex_)> lock(sharedMutex_);
                    ZoneScopedN("Render::SubmitCommands");
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }

                // 按钮触发: 锁风暴 (大量短时间锁竞争)
                if (triggerLockStorm_.load(std::memory_order_relaxed)) {
                    triggerLockStorm_.store(false, std::memory_order_relaxed);
                    TracyMessageL("Lock storm triggered on Render Thread");
                    for (int i = 0; i < 50; i++) {
                        std::lock_guard<decltype(sharedMutex_)> lock(sharedMutex_);
                        ZoneScopedN("Render::LockStorm");
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                    }
                }

                int sleepMs = 16 - renderDrawMs_;
                if (sleepMs > 0) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(sleepMs));
                }
            }
        });
    }

    void onDeactivate() override {
        running_ = false;
        if (ioThread_.joinable()) ioThread_.join();
        if (renderThread_.joinable()) renderThread_.join();
        telemetry::Manager::get().shutdown();

        // 清理泄漏的内存
        for (auto* p : leakedBlocks_) std::free(p);
        leakedBlocks_.clear();
        totalLeakedBytes_ = 0;
    }

    void onUpdate(float dt) override {
        ZoneScopedN("Main::GameLogic");

        // 基础计算负载
        volatile double x = 0;
        for (int i = 0; i < mainWorkIterations_; i++) {
            x += std::sin(static_cast<double>(i) * 0.001);
        }

        // ===== 按钮触发: 手动卡顿 =====
        if (triggerStutter_.load(std::memory_order_relaxed)) {
            triggerStutter_.store(false, std::memory_order_relaxed);
            TracyScopedContext("UserTriggered: ManualStutter");
            ZoneScopedN("Main::ManualStutter");
            TracyMessageL("Manual stutter triggered by user");
            // 阻塞主线程, 产生一个明显的卡顿
            std::this_thread::sleep_for(std::chrono::milliseconds(stutterMs_));
        }

        // ===== 按钮触发: GC Pause =====
        if (triggerGCPause_.load(std::memory_order_relaxed)) {
            triggerGCPause_.store(false, std::memory_order_relaxed);
            TracyScopedContext("UserTriggered: GC_FullCollect");
            ZoneScopedN("Main::GC_FullCollect");
            TracyMessageL("GC full collect triggered");
            // 模拟 GC 标记阶段
            {
                ZoneScopedN("GC::MarkPhase");
                volatile int sum = 0;
                for (int i = 0; i < 2000000; i++) sum += i;
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }
            // 模拟 GC 清扫阶段
            {
                ZoneScopedN("GC::SweepPhase");
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }

        // ===== 持续: 内存泄漏 =====
        if (leaking_) {
            ZoneScopedN("Main::MemoryLeak");
            for (int i = 0; i < leakBlocksPerFrame_; i++) {
                void* p = std::malloc(static_cast<size_t>(leakBlockSize_));
                if (p) {
                    std::memset(p, 0xCD, static_cast<size_t>(leakBlockSize_));
                    leakedBlocks_.push_back(p);
                    totalLeakedBytes_ += static_cast<size_t>(leakBlockSize_);
                }
            }
            TracyPlot("LeakedMB",
                static_cast<double>(totalLeakedBytes_) / (1024.0 * 1024.0));
        }

        // 周期性换装 (触发上下文 + 轻微卡顿)
        if (frameCounter_ % 120 == 0) {
            const char* asset = kAssetPaths[assetIndex_ % kAssetPathCount];
            TracyScopedContext(asset);
            ZoneScopedN("Main::SwitchOutfit");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            assetIndex_++;
        }

        // 周期性访问共享数据
        if (frameCounter_ % lockEveryNFrames_ == 0) {
            std::lock_guard<decltype(sharedMutex_)> lock(sharedMutex_);
            ZoneScopedN("Main::SharedDataAccess");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        frameCounter_++;
    }

    void onDrawUI() override {
        auto& mgr = telemetry::Manager::get();

        // ==========================================
        //  状态栏
        // ==========================================
        if (mgr.isActive()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                "[RECORDING]  Auto-export: %s  %s",
                autoExportSec_ > 0 ? "ON" : "OFF",
                mgr.config().stutterOnly ? "[StutterOnly]" : "");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "INACTIVE");
        }

        // 统计
        ImGui::Text("Events: %llu  Frames: %llu  Dropped: %llu",
            mgr.totalEvents(), mgr.frameCount(), mgr.totalDropped());

        const auto& stutters = mgr.stutters();
        if (!stutters.empty()) {
            const auto& last = stutters.back();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Stutters: %zu  Last: %.1f ms (#%llu)",
                stutters.size(), last.frameTimeMs, last.frameIndex);
            if (!last.context.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                    "  Context: %s", last.context.c_str());
            }
        } else {
            ImGui::Text("Stutters: 0");
        }

        ImGui::Separator();

        // ==========================================
        //  场景触发按钮
        // ==========================================
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
            "Scenario Triggers:");
        ImGui::Spacing();

        // --- 1. 手动卡顿 ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button("Trigger Stutter", ImVec2(180, 30))) {
            triggerStutter_.store(true, std::memory_order_relaxed);
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::SliderInt("##stutter_ms", &stutterMs_, 30, 500, "%d ms");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Block the main thread for N ms.\n"
                "Creates a visible STUTTER marker in Perfetto\n"
                "with context 'UserTriggered: ManualStutter'.");
        }

        // --- 2. 内存泄漏 ---
        ImGui::PushStyleColor(ImGuiCol_Button,
            leaking_ ? ImVec4(0.9f, 0.5f, 0.1f, 1.0f) : ImVec4(0.5f, 0.3f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button(leaking_ ? "Stop Leak" : "Start Memory Leak",
                          ImVec2(180, 30))) {
            leaking_ = !leaking_;
            if (leaking_) {
                TracyMessageL("Memory leak started");
            } else {
                TracyMessageL("Memory leak stopped");
            }
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (leaking_) {
            double mb = static_cast<double>(totalLeakedBytes_) / (1024.0 * 1024.0);
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                "LEAKING  %.1f MB (%zu blocks)", mb, leakedBlocks_.size());
        } else if (totalLeakedBytes_ > 0) {
            double mb = static_cast<double>(totalLeakedBytes_) / (1024.0 * 1024.0);
            ImGui::Text("Leaked total: %.1f MB", mb);
        } else {
            ImGui::TextDisabled("Click to start leaking memory");
        }

        // 泄漏参数
        if (leaking_ || totalLeakedBytes_ > 0) {
            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Blocks/frame", &leakBlocksPerFrame_, 1, 500);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Block size", &leakBlockSize_, 256, 65536, "%d B",
                             ImGuiSliderFlags_Logarithmic);

            if (totalLeakedBytes_ > 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Free All")) {
                    for (auto* p : leakedBlocks_) std::free(p);
                    leakedBlocks_.clear();
                    totalLeakedBytes_ = 0;
                    TracyMessageL("All leaked memory freed");
                }
            }

#ifdef _WIN32
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                ImGui::Text("Working Set: %.1f MB",
                    static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0));
            }
#endif
        }

        // --- 3. Heavy IO ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 1.0f, 1.0f));
        if (ImGui::Button("Heavy IO Load", ImVec2(180, 30))) {
            triggerHeavyIO_.store(true, std::memory_order_relaxed);
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextDisabled("IO thread loads 8 assets back-to-back");

        // --- 4. Lock Storm ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.9f, 1.0f));
        if (ImGui::Button("Lock Storm", ImVec2(180, 30))) {
            triggerLockStorm_.store(true, std::memory_order_relaxed);
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextDisabled("50x rapid lock/unlock on Render Thread");

        // --- 5. GC Pause ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.6f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.8f, 0.2f, 1.0f));
        if (ImGui::Button("GC Pause", ImVec2(180, 30))) {
            triggerGCPause_.store(true, std::memory_order_relaxed);
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextDisabled("Simulates GC Mark + Sweep (40ms pause)");

        ImGui::Separator();

        // ==========================================
        //  参数调节
        // ==========================================
        if (ImGui::CollapsingHeader("Thread Parameters")) {
            ImGui::Checkbox("Stutter-Only Mode", &stutterOnly_);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Only keep events around stutter frames.\n"
                    "Greatly reduces data volume in complex projects.\n"
                    "Requires restart (deactivate + activate) to apply.");
            }
            if (stutterOnly_) {
                ImGui::Indent();
                ImGui::SliderFloat("Capture Before (ms)",
                    &stutterCaptureBefore_, 10.0f, 500.0f, "%.0f");
                ImGui::SliderFloat("Capture After (ms)",
                    &stutterCaptureAfter_, 10.0f, 200.0f, "%.0f");
                ImGui::Unindent();
            }
            ImGui::SliderFloat("Stutter Threshold (ms)",
                &stutterThreshold_, 8.0f, 100.0f, "%.1f");
            ImGui::SliderInt("Main Work (iterations)",
                &mainWorkIterations_, 10000, 5000000);
            ImGui::SliderInt("IO Load (ms)", &ioLoadMs_, 1, 50);
            ImGui::SliderInt("Render Draw (ms)", &renderDrawMs_, 1, 15);
            ImGui::SliderInt("Lock every N frames", &lockEveryNFrames_, 1, 60);
        }

        if (ImGui::CollapsingHeader("Auto-Export & Upload")) {
            ImGui::SliderFloat("Export Interval (sec)",
                &autoExportSec_, 0.0f, 120.0f, "%.0f");
            ImGui::SliderInt("Max Trace Files", &maxTraceFiles_, 1, 50);
            ImGui::SliderInt("Upload Budget (MB)", &uploadBudgetMB_, 0, 500);
            if (uploadBudgetMB_ == 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("(unlimited)");
            }
        }

        ImGui::Separator();

        // ==========================================
        //  导出
        // ==========================================
        if (ImGui::Button("Export Trace Now", ImVec2(180, 0))) {
            mgr.exportTrace();
            exported_ = true;
        }
        if (exported_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Exported!");
        }

        ImGui::Separator();

        // 查看方法
        if (ImGui::CollapsingHeader("How to View")) {
            ImGui::BulletText("Output dir: %s/", mgr.config().outputDir.c_str());
            ImGui::BulletText("1. Open https://ui.perfetto.dev in Chrome");
            ImGui::BulletText("2. Drag the .json file into the browser");
            ImGui::BulletText("3. See multi-thread timeline + stutter marks");
            ImGui::BulletText("chrome://tracing also works");
        }
    }

private:
    std::atomic<bool> running_{false};
    std::thread ioThread_;
    std::thread renderThread_;
    telemetry::TrackedMutex<std::mutex> sharedMutex_{"SharedData"};

    int  frameCounter_ = 0;
    int  assetIndex_ = 0;
    bool exported_ = false;

    // --- 场景触发 ---
    std::atomic<bool> triggerStutter_{false};
    int               stutterMs_ = 100;
    bool              leaking_ = false;
    std::atomic<bool> triggerHeavyIO_{false};
    std::atomic<bool> triggerLockStorm_{false};
    std::atomic<bool> triggerGCPause_{false};

    // --- 内存泄漏 ---
    std::vector<void*> leakedBlocks_;
    size_t             totalLeakedBytes_ = 0;
    int                leakBlocksPerFrame_ = 10;
    int                leakBlockSize_ = 4096;

    // --- 线程参数 ---
    float stutterThreshold_ = 33.3f;
    int   mainWorkIterations_ = 500000;
    int   ioLoadMs_ = 5;
    int   renderDrawMs_ = 8;
    int   lockEveryNFrames_ = 10;
    float autoExportSec_ = 30.0f;
    int   maxTraceFiles_ = 10;
    int   uploadBudgetMB_ = 0;

    // --- stutterOnly ---
    bool  stutterOnly_ = false;
    float stutterCaptureBefore_ = 100.0f;
    float stutterCaptureAfter_  = 50.0f;
};

ADD_PERF_CASE(TelemetryDemoCase)
