/*
 * 03_object_pooling.cpp - 对象池优化
 *
 * 学习要点:
 * 1. 频繁 new/delete 的性能问题 (内存碎片 + 分配器开销)
 * 2. 对象池 (Object Pool) 模式: 预分配 + 复用
 * 3. 固定大小池 vs 可增长池
 * 4. 真实场景: 粒子系统、子弹、网络消息
 *
 * 对象池是帧率优化中最实用的技术之一
 */

#include "profiler.h"
#include <vector>
#include <queue>
#include <memory>
#include <cstring>
#include <iostream>

// ============================================================
// 测试用结构: 粒子
// ============================================================
struct Particle {
    float x, y, z;
    float vx, vy, vz;
    float life;
    float size;
    float color[4]; // RGBA
    char  tag[32];

    void init(float px, float py) {
        x = px; y = py; z = 0;
        vx = 1.0f; vy = -2.0f; vz = 0;
        life = 1.0f;
        size = 5.0f;
        color[0] = color[1] = color[2] = color[3] = 1.0f;
        std::memset(tag, 0, sizeof(tag));
    }

    void update(float dt) {
        x += vx * dt; y += vy * dt; z += vz * dt;
        life -= dt;
    }

    bool isDead() const { return life <= 0; }
};

// ============================================================
// 对象池实现
// ============================================================
template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t initialCapacity = 1024) {
        // 预分配整块内存
        storage_.resize(initialCapacity);
        for (size_t i = 0; i < initialCapacity; ++i) {
            freeList_.push(&storage_[i]);
        }
    }

    T* acquire() {
        if (freeList_.empty()) {
            // 池耗尽时扩展
            size_t oldSize = storage_.size();
            size_t newSize = oldSize * 2;
            storage_.resize(newSize);
            for (size_t i = oldSize; i < newSize; ++i) {
                freeList_.push(&storage_[i]);
            }
        }
        T* obj = freeList_.front();
        freeList_.pop();
        return obj;
    }

    void release(T* obj) {
        freeList_.push(obj);
    }

    size_t capacity() const { return storage_.size(); }
    size_t available() const { return freeList_.size(); }
    size_t inUse() const { return capacity() - available(); }

private:
    std::vector<T> storage_;
    std::queue<T*> freeList_;
};

// ============================================================
// 测试1: new/delete vs 对象池 - 基本性能
// ============================================================
void test_basic_performance() {
    perf::printHeader("new/delete vs Object Pool: Basic Performance");

    constexpr int N = 100000;

    // 方式1: 每次 new/delete
    auto heap_result = perf::benchmark("Heap new/delete", [&]() {
        for (int i = 0; i < N; ++i) {
            Particle* p = new Particle();
            p->init(static_cast<float>(i), 0);
            p->update(0.016f);
            delete p;
        }
    }, 20);

    // 方式2: 对象池
    ObjectPool<Particle> pool(N);

    auto pool_result = perf::benchmark("Object Pool", [&]() {
        for (int i = 0; i < N; ++i) {
            Particle* p = pool.acquire();
            p->init(static_cast<float>(i), 0);
            p->update(0.016f);
            pool.release(p);
        }
    }, 20);

    perf::printComparison("Heap", heap_result.avg_us,
                          "Pool", pool_result.avg_us);
}

// ============================================================
// 测试2: 模拟粒子系统 - 持续创建和销毁
// ============================================================
void test_particle_system() {
    perf::printHeader("Particle System Simulation (create/destroy per frame)");

    constexpr int FRAMES = 100;
    constexpr int PARTICLES_PER_FRAME = 500;  // 每帧创建500个粒子
    constexpr float DT = 0.016f;

    // 方式1: 动态分配
    auto heap_result = perf::benchmark("Heap-based particles", [&]() {
        std::vector<Particle*> active;
        active.reserve(PARTICLES_PER_FRAME * 10);

        for (int frame = 0; frame < FRAMES; ++frame) {
            // 创建新粒子
            for (int i = 0; i < PARTICLES_PER_FRAME; ++i) {
                Particle* p = new Particle();
                p->init(static_cast<float>(i), static_cast<float>(frame));
                active.push_back(p);
            }

            // 更新所有粒子
            for (auto* p : active) {
                p->update(DT);
            }

            // 移除死亡粒子
            auto it = active.begin();
            while (it != active.end()) {
                if ((*it)->isDead()) {
                    delete *it;
                    it = active.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // 清理剩余
        for (auto* p : active) delete p;
    }, 5);

    // 方式2: 对象池
    auto pool_result = perf::benchmark("Pool-based particles", [&]() {
        ObjectPool<Particle> pool(PARTICLES_PER_FRAME * 20);
        std::vector<Particle*> active;
        active.reserve(PARTICLES_PER_FRAME * 10);

        for (int frame = 0; frame < FRAMES; ++frame) {
            for (int i = 0; i < PARTICLES_PER_FRAME; ++i) {
                Particle* p = pool.acquire();
                p->init(static_cast<float>(i), static_cast<float>(frame));
                active.push_back(p);
            }

            for (auto* p : active) {
                p->update(DT);
            }

            auto it = active.begin();
            while (it != active.end()) {
                if ((*it)->isDead()) {
                    pool.release(*it);
                    it = active.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }, 5);

    perf::printComparison("Heap particles", heap_result.avg_us,
                          "Pool particles", pool_result.avg_us);
}

// ============================================================
// 测试3: 内存碎片效应
// ============================================================
void test_fragmentation() {
    perf::printHeader("Memory Fragmentation Effect");

    constexpr int N = 50000;

    // 制造碎片: 交替分配/释放不同大小的对象
    std::cout << "  Pre-fragmenting heap...\n";
    std::vector<void*> ptrs;
    for (int i = 0; i < N; ++i) {
        int sizes[] = {16, 64, 256, 1024, 48};
        ptrs.push_back(::operator new(sizes[i % 5]));
    }
    // 释放一半 (制造空洞)
    for (int i = 0; i < N; i += 2) {
        ::operator delete(ptrs[i]);
        ptrs[i] = nullptr;
    }

    // 在碎片化的堆上分配
    auto fragmented_result = perf::benchmark("Allocation on fragmented heap", [&]() {
        std::vector<Particle*> particles;
        for (int i = 0; i < N / 2; ++i) {
            particles.push_back(new Particle());
        }
        for (auto* p : particles) delete p;
    }, 10);

    // 清理碎片
    for (auto* p : ptrs) {
        if (p) ::operator delete(p);
    }

    // 在干净的堆上分配 (对比)
    auto clean_result = perf::benchmark("Allocation on clean heap", [&]() {
        std::vector<Particle*> particles;
        for (int i = 0; i < N / 2; ++i) {
            particles.push_back(new Particle());
        }
        for (auto* p : particles) delete p;
    }, 10);

    // 对象池 (不受碎片影响)
    ObjectPool<Particle> pool(N / 2);
    auto pool_result = perf::benchmark("Object Pool (no fragmentation)", [&]() {
        std::vector<Particle*> particles;
        for (int i = 0; i < N / 2; ++i) {
            particles.push_back(pool.acquire());
        }
        for (auto* p : particles) pool.release(p);
    }, 10);

    std::cout << "\n  对象池的优势:\n"
              << "  1. 连续内存, 无碎片\n"
              << "  2. 分配/释放是 O(1) 操作\n"
              << "  3. 缓存友好 (对象紧密排列)\n";
}

int main() {
    perf::initConsole();
    std::cout << "===== 03: Object Pooling Tutorial =====\n";
    std::cout << "sizeof(Particle) = " << sizeof(Particle) << " bytes\n\n";

    test_basic_performance();
    test_particle_system();
    test_fragmentation();

    std::cout << "\n===== 适用场景 =====\n"
              << "1. 粒子系统 - 大量短生命周期对象\n"
              << "2. 网络消息/数据包 - 固定大小的缓冲区\n"
              << "3. 子弹/投射物 - 游戏中频繁创建销毁\n"
              << "4. UI 元素复用 - 列表/网格虚拟化\n"
              << "5. 数据库连接池 - 避免重复建立连接\n";

    return 0;
}
