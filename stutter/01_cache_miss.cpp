/*
 * 01_cache_miss.cpp - CPU 缓存未命中导致的性能问题
 *
 * 学习要点:
 * 1. CPU 缓存原理: L1/L2/L3 缓存层级
 * 2. 连续内存 vs 随机访问的性能差异
 * 3. AoS (Array of Structs) vs SoA (Struct of Arrays) 布局
 * 4. 数据局部性 (Data Locality) 的重要性
 *
 * 缓存未命中是"莫名其妙卡顿"的常见原因之一
 */

#include "profiler.h"
#include <vector>
#include <random>
#include <algorithm>
#include <numeric>
#include <cstring>

// ============================================================
// 测试1: 顺序访问 vs 随机访问
// ============================================================
void test_sequential_vs_random() {
    perf::printHeader("Sequential vs Random Access");

    constexpr int N = 1024 * 1024 * 4; // 4M 个整数 = 16MB
    std::vector<int> data(N);
    std::iota(data.begin(), data.end(), 0);

    // 构建随机访问索引
    std::vector<int> indices(N);
    std::iota(indices.begin(), indices.end(), 0);

    std::mt19937 rng(42);
    std::shuffle(indices.begin(), indices.end(), rng);

    volatile long long sum = 0; // volatile 防止优化掉

    // 顺序访问 - 缓存友好
    auto seq_result = perf::benchmark("Sequential access", [&]() {
        long long s = 0;
        for (int i = 0; i < N; ++i) {
            s += data[i];
        }
        sum = s;
    }, 20);

    // 随机访问 - 缓存不友好
    auto rand_result = perf::benchmark("Random access", [&]() {
        long long s = 0;
        for (int i = 0; i < N; ++i) {
            s += data[indices[i]];
        }
        sum = s;
    }, 20);

    perf::printComparison("Random", rand_result.avg_us,
                          "Sequential", seq_result.avg_us);

    std::cout << "  结论: 随机访问通常比顺序访问慢 3-10 倍\n"
              << "  原因: CPU 缓存行 (64 bytes) 预取在顺序访问时非常高效\n";
}

// ============================================================
// 测试2: AoS vs SoA 内存布局
// ============================================================

// AoS: Array of Structs (传统面向对象布局)
struct ParticleAoS {
    float x, y, z;        // 位置
    float vx, vy, vz;     // 速度
    float r, g, b, a;     // 颜色
    float life;            // 生命值
    float size;            // 大小
    // 每个结构体 48 字节
};

// SoA: Struct of Arrays (面向数据布局)
struct ParticlesSoA {
    std::vector<float> x, y, z;
    std::vector<float> vx, vy, vz;
    std::vector<float> r, g, b, a;
    std::vector<float> life;
    std::vector<float> size;

    void resize(size_t n) {
        x.resize(n); y.resize(n); z.resize(n);
        vx.resize(n); vy.resize(n); vz.resize(n);
        r.resize(n); g.resize(n); b.resize(n); a.resize(n);
        life.resize(n);
        size.resize(n);
    }
};

void test_aos_vs_soa() {
    perf::printHeader("AoS vs SoA Memory Layout");

    constexpr int N = 500000;

    // ---- AoS 版本 ----
    std::vector<ParticleAoS> aos(N);
    for (auto& p : aos) {
        p.x = 1.0f; p.vx = 0.1f;
        p.y = 2.0f; p.vy = 0.2f;
        p.z = 3.0f; p.vz = 0.3f;
        p.life = 1.0f;
    }

    // ---- SoA 版本 ----
    ParticlesSoA soa;
    soa.resize(N);
    for (int i = 0; i < N; ++i) {
        soa.x[i] = 1.0f; soa.vx[i] = 0.1f;
        soa.y[i] = 2.0f; soa.vy[i] = 0.2f;
        soa.z[i] = 3.0f; soa.vz[i] = 0.3f;
        soa.life[i] = 1.0f;
    }

    // 只更新位置 (x += vx * dt)
    // AoS: 每加载一个缓存行，只用其中的 x 和 vx 字段 (8/48 字节)
    auto aos_result = perf::benchmark("AoS: update positions", [&]() {
        for (int i = 0; i < N; ++i) {
            aos[i].x += aos[i].vx * 0.016f;
            aos[i].y += aos[i].vy * 0.016f;
            aos[i].z += aos[i].vz * 0.016f;
        }
    }, 50);

    // SoA: 每加载一个缓存行，全部都是需要的数据
    auto soa_result = perf::benchmark("SoA: update positions", [&]() {
        for (int i = 0; i < N; ++i) {
            soa.x[i] += soa.vx[i] * 0.016f;
            soa.y[i] += soa.vy[i] * 0.016f;
            soa.z[i] += soa.vz[i] * 0.016f;
        }
    }, 50);

    perf::printComparison("AoS", aos_result.avg_us,
                          "SoA", soa_result.avg_us);

    std::cout << "  结论: 当只需要结构体的部分字段时, SoA 布局缓存效率更高\n"
              << "  AoS 每个缓存行浪费: " << (1.0 - 24.0/48.0) * 100 << "% 的带宽\n";
}

// ============================================================
// 测试3: 矩阵遍历方向
// ============================================================
void test_matrix_traversal() {
    perf::printHeader("Matrix Traversal: Row-major vs Column-major");

    constexpr int ROWS = 4096;
    constexpr int COLS = 4096;

    // 使用一维数组模拟二维矩阵 (行主序存储)
    std::vector<int> matrix(ROWS * COLS, 1);
    volatile long long sum = 0;

    // 行主序遍历 (缓存友好 - 内存连续)
    auto row_result = perf::benchmark("Row-major traversal", [&]() {
        long long s = 0;
        for (int r = 0; r < ROWS; ++r) {
            for (int c = 0; c < COLS; ++c) {
                s += matrix[r * COLS + c];
            }
        }
        sum = s;
    }, 10);

    // 列主序遍历 (缓存不友好 - 每次跳跃 COLS * sizeof(int) 字节)
    auto col_result = perf::benchmark("Column-major traversal", [&]() {
        long long s = 0;
        for (int c = 0; c < COLS; ++c) {
            for (int r = 0; r < ROWS; ++r) {
                s += matrix[r * COLS + c];
            }
        }
        sum = s;
    }, 10);

    perf::printComparison("Column-major", col_result.avg_us,
                          "Row-major", row_result.avg_us);

    std::cout << "  结论: C/C++ 数组是行主序, 应该先遍历列 (内层循环)\n"
              << "  每次列跳跃距离: " << COLS * sizeof(int) << " bytes\n"
              << "  缓存行大小: 64 bytes -> 大量 cache miss\n";
}

int main() {
    perf::initConsole();
    std::cout << "===== 01: CPU Cache Miss Tutorial =====\n";
    std::cout << "Understanding cache hierarchy and data locality\n";

    test_sequential_vs_random();
    test_aos_vs_soa();
    test_matrix_traversal();

    std::cout << "\n===== 优化建议 =====\n"
              << "1. 优先使用连续内存容器 (vector > list/map)\n"
              << "2. 热数据紧凑排列, 冷数据分离\n"
              << "3. 遍历顺序与内存布局一致\n"
              << "4. 考虑 SoA 替代 AoS (尤其是大批量更新)\n"
              << "5. 使用 Windows Performance Analyzer (WPA) 查看 cache miss 指标\n";

    return 0;
}
