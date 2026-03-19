/*
 * 02_smart_ptr_fix.cpp - 使用智能指针修复内存泄漏
 *
 * 学习要点:
 * 1. std::unique_ptr - 独占所有权, 自动释放
 * 2. std::shared_ptr - 共享所有权, 引用计数
 * 3. std::make_unique / std::make_shared 的优势
 * 4. 与裸指针的性能对比
 *
 * 关键原则: 现代C++中几乎不应该直接使用 new/delete
 */

#include "profiler.h"
#include "memory_tracker.h"
#include <memory>
#include <vector>
#include <string>

struct Particle {
    float x, y, z;
    float vx, vy, vz;
    float life;

    Particle() : x(0), y(0), z(0), vx(0), vy(0), vz(0), life(1.0f) {}

    void update(float dt) {
        x += vx * dt;
        y += vy * dt;
        z += vz * dt;
        life -= dt * 0.1f;
    }
};

// ============================================================
// 示例1: unique_ptr - 异常安全，自动释放
// ============================================================
void demo_unique_ptr() {
    perf::printHeader("unique_ptr - Exception Safe");

    // 不会泄漏！即使抛出异常也会被正确释放
    auto particle = std::make_unique<Particle>();
    particle->x = 10.0f;
    particle->vx = 1.0f;

    std::cout << "  Particle at (" << particle->x << ", " << particle->y << ")\n";

    try {
        // 即使这里抛异常，particle 也会被释放
        throw std::runtime_error("simulated error");
    } catch (...) {
        std::cout << "  Exception caught, but no leak!\n";
    }
    // particle 在离开作用域时自动 delete
}

// ============================================================
// 示例2: unique_ptr 数组
// ============================================================
void demo_unique_ptr_array() {
    perf::printHeader("unique_ptr for Arrays");

    constexpr int COUNT = 10000;

    // 使用 unique_ptr 管理数组
    auto particles = std::make_unique<Particle[]>(COUNT);

    for (int i = 0; i < COUNT; ++i) {
        particles[i].x = static_cast<float>(i);
        particles[i].update(0.016f);
    }

    std::cout << "  Updated " << COUNT << " particles (auto-freed)\n";
    // 离开作用域时自动释放整个数组
}

// ============================================================
// 示例3: shared_ptr 在容器中
// ============================================================
void demo_shared_ptr_container() {
    perf::printHeader("shared_ptr in Containers");

    std::vector<std::shared_ptr<Particle>> particles;

    for (int i = 0; i < 100; ++i) {
        auto p = std::make_shared<Particle>();
        p->x = static_cast<float>(i);
        particles.push_back(p);
    }

    std::cout << "  Created " << particles.size() << " shared particles\n";
    std::cout << "  Reference count of [0]: " << particles[0].use_count() << "\n";

    // 复制一些引用
    auto copy = particles[0];
    std::cout << "  After copy, ref count: " << particles[0].use_count() << "\n";

    // 清空容器 - 引用计数减少，最后一个引用释放时自动 delete
    particles.clear();
    std::cout << "  After clear, copy ref count: " << copy.use_count() << "\n";
    // copy 离开作用域后，最后的引用消失，Particle 被自动释放
}

// ============================================================
// 性能对比: 裸指针 vs unique_ptr vs shared_ptr
// ============================================================
void performance_comparison() {
    perf::printHeader("Performance Comparison: Raw vs Smart Pointers");

    constexpr int N = 100000;

    // 裸指针
    auto raw_result = perf::benchmark("Raw pointer new/delete", [&]() {
        for (int i = 0; i < N; ++i) {
            Particle* p = new Particle();
            p->update(0.016f);
            delete p;
        }
    }, 10);

    // unique_ptr
    auto unique_result = perf::benchmark("unique_ptr make/destroy", [&]() {
        for (int i = 0; i < N; ++i) {
            auto p = std::make_unique<Particle>();
            p->update(0.016f);
        }
    }, 10);

    // shared_ptr
    auto shared_result = perf::benchmark("shared_ptr make/destroy", [&]() {
        for (int i = 0; i < N; ++i) {
            auto p = std::make_shared<Particle>();
            p->update(0.016f);
        }
    }, 10);

    std::cout << "\n--- Summary ---\n"
              << "  unique_ptr overhead vs raw: "
              << (unique_result.avg_us / raw_result.avg_us - 1.0) * 100 << "%\n"
              << "  shared_ptr overhead vs raw: "
              << (shared_result.avg_us / raw_result.avg_us - 1.0) * 100 << "%\n"
              << "  结论: unique_ptr 几乎零开销, shared_ptr 有引用计数开销\n";
}

int main() {
    perf::initConsole();
    std::cout << "===== 02: Smart Pointer Solutions =====\n\n";

    demo_unique_ptr();
    demo_unique_ptr_array();
    demo_shared_ptr_container();
    performance_comparison();

#ifdef _WIN32
    perf::MemoryTracker::printProcessMemory();
#endif

    return 0;
}
