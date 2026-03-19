/*
 * 02_false_sharing.cpp - 伪共享 (False Sharing) 导致的多线程性能问题
 *
 * 学习要点:
 * 1. 什么是伪共享: 不同线程写入同一缓存行的不同变量
 * 2. 为什么每个核心的 L1 缓存会互相失效
 * 3. 缓存行对齐 (alignas(64)) 解决方案
 * 4. 真实性能差异可达 2-10 倍
 *
 * 这是多线程程序"越多线程越慢"的常见原因
 */

#include "profiler.h"
#include <thread>
#include <vector>
#include <atomic>
#include <cstdint>

// 缓存行大小通常为 64 字节
constexpr size_t CACHE_LINE_SIZE = 64;

// ============================================================
// BAD: 伪共享 - 计数器紧密排列
// 多个线程写入相邻的内存位置，导致缓存行反复失效
// ============================================================
struct CountersBad {
    int64_t counter[8]; // 8个计数器紧密排列, 全在1-2个缓存行内
};

// ============================================================
// GOOD: 每个计数器独占一个缓存行
// ============================================================
struct alignas(CACHE_LINE_SIZE) PaddedCounter {
    int64_t value;
    // padding 自动填充到 64 字节
};

struct CountersGood {
    PaddedCounter counter[8]; // 每个计数器占独立缓存行
};

// ============================================================
// 工作函数: 频繁写入自己的计数器
// ============================================================
constexpr int64_t ITERATIONS = 50'000'000;

void worker_bad(CountersBad& counters, int id) {
    for (int64_t i = 0; i < ITERATIONS; ++i) {
        counters.counter[id]++;
    }
}

void worker_good(CountersGood& counters, int id) {
    for (int64_t i = 0; i < ITERATIONS; ++i) {
        counters.counter[id].value++;
    }
}

// ============================================================
// 测试: 不同线程数下的对比
// ============================================================
void test_false_sharing(int numThreads) {
    std::cout << "\n--- " << numThreads << " threads ---\n";

    // BAD: 伪共享版本
    auto bad_result = perf::benchmark("False sharing (BAD)", [&]() {
        CountersBad counters = {};
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back(worker_bad, std::ref(counters), i);
        }
        for (auto& t : threads) t.join();
    }, 5);

    // GOOD: 缓存行对齐版本
    auto good_result = perf::benchmark("Cache-aligned (GOOD)", [&]() {
        CountersGood counters = {};
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back(worker_good, std::ref(counters), i);
        }
        for (auto& t : threads) t.join();
    }, 5);

    perf::printComparison("False sharing", bad_result.avg_us,
                          "Cache-aligned", good_result.avg_us);
}

// ============================================================
// 额外: atomic 变量的伪共享
// ============================================================
struct AtomicBad {
    std::atomic<int64_t> counters[8]; // 紧密排列
};

struct alignas(CACHE_LINE_SIZE) PaddedAtomic {
    std::atomic<int64_t> value{0};
};

struct AtomicGood {
    PaddedAtomic counters[8];
};

void test_atomic_false_sharing() {
    perf::printHeader("Atomic Variables with False Sharing");
    const int64_t ATOMIC_ITERS = 10'000'000;
    int numThreads = 4;

    auto bad_result = perf::benchmark("Atomic false sharing", [&]() {
        AtomicBad counters = {};
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&counters, i, ATOMIC_ITERS]() {
                for (int64_t j = 0; j < ATOMIC_ITERS; ++j) {
                    counters.counters[i].fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& t : threads) t.join();
    }, 5);

    auto good_result = perf::benchmark("Atomic cache-aligned", [&]() {
        AtomicGood counters = {};
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&counters, i, ATOMIC_ITERS]() {
                for (int64_t j = 0; j < ATOMIC_ITERS; ++j) {
                    counters.counters[i].value.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& t : threads) t.join();
    }, 5);

    perf::printComparison("Atomic false sharing", bad_result.avg_us,
                          "Atomic aligned", good_result.avg_us);
}

int main() {
    perf::initConsole();
    std::cout << "===== 02: False Sharing Tutorial =====\n";

    // 显示系统信息
    unsigned int hwThreads = std::thread::hardware_concurrency();
    std::cout << "Hardware threads: " << hwThreads << "\n";
    std::cout << "Cache line size : " << CACHE_LINE_SIZE << " bytes\n";
    std::cout << "sizeof(CountersBad) : " << sizeof(CountersBad) << " bytes\n";
    std::cout << "sizeof(CountersGood): " << sizeof(CountersGood) << " bytes\n\n";

    perf::printHeader("Non-atomic counter comparison");
    test_false_sharing(2);
    test_false_sharing(4);
    if (hwThreads >= 8) {
        test_false_sharing(8);
    }

    test_atomic_false_sharing();

    std::cout << "\n===== 优化建议 =====\n"
              << "1. 多线程写入的数据, 每线程使用 alignas(64) 对齐\n"
              << "2. C++17: 使用 std::hardware_destructive_interference_size\n"
              << "3. 线程局部累加后再合并 (reduce pattern)\n"
              << "4. 使用 VTune / WPA 检测 cache-line contention\n";

    return 0;
}
