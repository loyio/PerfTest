/*
 * 03_lock_contention.cpp - 锁竞争导致的性能问题
 *
 * 学习要点:
 * 1. 全局锁 (coarse-grained) vs 细粒度锁 (fine-grained)
 * 2. 锁竞争如何导致线程阻塞和卡顿
 * 3. 无锁数据结构 (lock-free) 的思路
 * 4. 读写锁 (shared_mutex) 优化读多写少场景
 *
 * 锁竞争是多线程程序卡顿的第二大原因
 */

#include "profiler.h"
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <unordered_map>
#include <string>
#include <atomic>
#include <random>

constexpr int NUM_THREADS = 4;
constexpr int OPS_PER_THREAD = 100000;

// ============================================================
// 测试1: 粗粒度锁 vs 细粒度锁
// ============================================================

// BAD: 一把大锁保护所有数据
class CoarseGrainedCache {
public:
    void put(int key, int value) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_[key] = value;
    }

    int get(int key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        return (it != data_.end()) ? it->second : -1;
    }

private:
    std::mutex mutex_;
    std::unordered_map<int, int> data_;
};

// GOOD: 分片锁 (sharded lock)
class ShardedCache {
    static constexpr int NUM_SHARDS = 16;

    struct Shard {
        std::mutex mutex;
        std::unordered_map<int, int> data;
    };

public:
    void put(int key, int value) {
        auto& shard = shards_[key % NUM_SHARDS];
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.data[key] = value;
    }

    int get(int key) {
        auto& shard = shards_[key % NUM_SHARDS];
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto it = shard.data.find(key);
        return (it != shard.data.end()) ? it->second : -1;
    }

private:
    Shard shards_[NUM_SHARDS];
};

void test_coarse_vs_sharded() {
    perf::printHeader("Coarse-grained Lock vs Sharded Lock");

    // 粗粒度锁
    auto coarse_result = perf::benchmark("Coarse-grained lock", [&]() {
        CoarseGrainedCache cache;
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&cache, t]() {
                std::mt19937 rng(t);
                for (int i = 0; i < OPS_PER_THREAD; ++i) {
                    int key = rng() % 1000;
                    if (i % 3 == 0)
                        cache.put(key, i);
                    else
                        cache.get(key);
                }
            });
        }
        for (auto& th : threads) th.join();
    }, 5);

    // 分片锁
    auto sharded_result = perf::benchmark("Sharded lock", [&]() {
        ShardedCache cache;
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&cache, t]() {
                std::mt19937 rng(t);
                for (int i = 0; i < OPS_PER_THREAD; ++i) {
                    int key = rng() % 1000;
                    if (i % 3 == 0)
                        cache.put(key, i);
                    else
                        cache.get(key);
                }
            });
        }
        for (auto& th : threads) th.join();
    }, 5);

    perf::printComparison("Coarse-grained", coarse_result.avg_us,
                          "Sharded", sharded_result.avg_us);
}

// ============================================================
// 测试2: mutex vs shared_mutex (读写锁)
// ============================================================

class MutexCounter {
public:
    void increment() {
        std::lock_guard<std::mutex> lock(mutex_);
        value_++;
    }

    int read() {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

private:
    std::mutex mutex_;
    int value_ = 0;
};

class RWLockCounter {
public:
    void increment() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        value_++;
    }

    int read() {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return value_;
    }

private:
    std::shared_mutex mutex_;
    int value_ = 0;
};

void test_mutex_vs_rwlock() {
    perf::printHeader("mutex vs shared_mutex (Read-heavy workload: 90% read)");

    const int TOTAL_OPS = 500000;

    // 普通 mutex
    auto mutex_result = perf::benchmark("std::mutex", [&]() {
        MutexCounter counter;
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&counter, TOTAL_OPS]() {
                volatile int sink = 0;
                for (int i = 0; i < TOTAL_OPS; ++i) {
                    if (i % 10 == 0)
                        counter.increment();
                    else
                        sink = counter.read();
                }
            });
        }
        for (auto& th : threads) th.join();
    }, 5);

    // 读写锁
    auto rwlock_result = perf::benchmark("std::shared_mutex", [&]() {
        RWLockCounter counter;
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&counter, TOTAL_OPS]() {
                volatile int sink = 0;
                for (int i = 0; i < TOTAL_OPS; ++i) {
                    if (i % 10 == 0)
                        counter.increment();
                    else
                        sink = counter.read();
                }
            });
        }
        for (auto& th : threads) th.join();
    }, 5);

    perf::printComparison("mutex", mutex_result.avg_us,
                          "shared_mutex", rwlock_result.avg_us);

    std::cout << "  结论: 读多写少时, shared_mutex 允许多个读并行\n";
}

// ============================================================
// 测试3: 线程本地累加 (Thread-local accumulation)
// ============================================================
void test_thread_local_reduction() {
    perf::printHeader("Lock-based vs Thread-local Reduction");

    constexpr int64_t N = 10'000'000;

    // BAD: 每次操作都加锁
    auto lock_result = perf::benchmark("Lock-based sum", [&]() {
        std::mutex mutex;
        int64_t total = 0;
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&mutex, &total, t, N]() {
                int64_t perThread = N / NUM_THREADS;
                for (int64_t i = 0; i < perThread; ++i) {
                    std::lock_guard<std::mutex> lock(mutex);
                    total += 1;
                }
            });
        }
        for (auto& th : threads) th.join();
    }, 3);

    // GOOD: 线程本地累加后合并
    auto local_result = perf::benchmark("Thread-local sum", [&]() {
        std::atomic<int64_t> total{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&total, t, N]() {
                int64_t localSum = 0; // 线程本地变量
                int64_t perThread = N / NUM_THREADS;
                for (int64_t i = 0; i < perThread; ++i) {
                    localSum += 1; // 无锁!
                }
                total.fetch_add(localSum); // 只在最后合并一次
            });
        }
        for (auto& th : threads) th.join();
    }, 3);

    perf::printComparison("Lock-based", lock_result.avg_us,
                          "Thread-local", local_result.avg_us);

    std::cout << "  结论: 减少锁的获取频率是最有效的优化\n";
}

int main() {
    perf::initConsole();
    std::cout << "===== 03: Lock Contention Tutorial =====\n";
    std::cout << "Hardware threads: " << std::thread::hardware_concurrency() << "\n";

    test_coarse_vs_sharded();
    test_mutex_vs_rwlock();
    test_thread_local_reduction();

    std::cout << "\n===== 优化建议 =====\n"
              << "1. 减少锁的持有时间 (只锁必要的代码)\n"
              << "2. 减少锁的粒度 (分片锁)\n"
              << "3. 读多写少用 shared_mutex\n"
              << "4. 线程本地累加后再合并\n"
              << "5. 考虑无锁数据结构 (仅在确实需要时)\n"
              << "6. 使用 Windows Performance Recorder 分析锁竞争\n";

    return 0;
}
