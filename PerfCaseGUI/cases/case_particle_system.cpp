/*
 * case_particle_system.cpp - 粒子系统负载
 *
 * 持续模式: 每帧更新粒子位置，并在 ImGui 中可视化。
 * 综合测试: 内存带宽 + 计算 + 绘制开销。
 */

#include "perf_case.h"
#include "tracy_integration.h"
#include "imgui.h"
#include <vector>
#include <random>
#include <algorithm>

class ParticleSystemCase : public PerfCase {
public:
    const char* getCategory()    const override { return "Framerate"; }
    const char* getName()        const override { return "Particle System"; }
    const char* getDescription() const override {
        return "Particle simulation with ImGui visualization.\n"
               "Combined CPU (update) + draw overhead.\n"
               "Increase count to stress memory bandwidth.";
    }

    void onActivate() override {
        rebuild();
    }

    void onDeactivate() override {
        particles_.clear();
        particles_.shrink_to_fit();
    }

    void onUpdate(float dt) override {
        ZoneScopedN("ParticleUpdate");
        if (static_cast<int>(particles_.size()) != count_) rebuild();

        for (auto& p : particles_) {
            p.x  += p.vx * dt * 60.0f;
            p.y  += p.vy * dt * 60.0f;
            p.vy += 0.0001f; // gravity
            p.life -= dt * 0.5f;

            if (p.x < -1.0f || p.x > 1.0f) p.vx = -p.vx;
            if (p.y < -1.0f || p.y > 1.0f) p.vy = -p.vy;

            if (p.life <= 0) {
                p.x = 0; p.y = 0;
                p.vx = velDist_(rng_);
                p.vy = velDist_(rng_);
                p.life = 1.0f;
            }
        }
    }

    void onDrawUI() override {
        bool changed = ImGui::SliderInt("Count", &count_, 1000, 1000000, "%d",
                                        ImGuiSliderFlags_Logarithmic);
        if (changed && active) rebuild();

        ImGui::SliderInt("Max Draw", &maxDraw_, 1000, 100000, "%d",
                         ImGuiSliderFlags_Logarithmic);

        if (active) {
            ImGui::Text("Update: %.2f ms | Particles: %d",
                        lastUpdateMs, (int)particles_.size());
        }

        // 可视化
        if (!particles_.empty()) {
            ImGui::Separator();

            ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
            ImVec2 canvasSize = ImGui::GetContentRegionAvail();
            canvasSize.y = (std::max)(canvasSize.y, 200.0f);
            canvasSize.y = (std::min)(canvasSize.y, 500.0f);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(canvasPos,
                ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                IM_COL32(15, 15, 25, 255));

            float cx = canvasPos.x + canvasSize.x * 0.5f;
            float cy = canvasPos.y + canvasSize.y * 0.5f;
            float scale = (std::min)(canvasSize.x, canvasSize.y) * 0.45f;

            int drawCount = (std::min)(static_cast<int>(particles_.size()), maxDraw_);
            for (int i = 0; i < drawCount; i++) {
                auto& p = particles_[i];
                float sx = cx + p.x * scale;
                float sy = cy + p.y * scale;
                ImU32 col = IM_COL32(
                    (int)(p.r * 255), (int)(p.g * 255), (int)(p.b * 255),
                    (int)(p.life * 200 + 55));
                dl->AddCircleFilled(ImVec2(sx, sy), 2.0f, col);
            }

            ImGui::Dummy(canvasSize);
        }
    }

private:
    int count_   = 10000;
    int maxDraw_ = 50000;

    struct Particle {
        float x, y, vx, vy, life;
        float r, g, b;
    };
    std::vector<Particle> particles_;

    std::mt19937 rng_{42};
    std::uniform_real_distribution<float> posDist_{-1.0f, 1.0f};
    std::uniform_real_distribution<float> velDist_{-0.01f, 0.01f};
    std::uniform_real_distribution<float> colDist_{0.2f, 1.0f};

    void rebuild() {
        particles_.resize(count_);
        for (auto& p : particles_) {
            p.x = posDist_(rng_); p.y = posDist_(rng_);
            p.vx = velDist_(rng_); p.vy = velDist_(rng_);
            p.life = colDist_(rng_);
            p.r = colDist_(rng_); p.g = colDist_(rng_); p.b = colDist_(rng_);
        }
    }
};

ADD_PERF_CASE(ParticleSystemCase)
