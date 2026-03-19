#pragma once
#include <iostream>
#include <unordered_map>
#include <string>
#include <mutex>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace perf {

// ============================================================
// MemoryTracker - 追踪内存分配/释放，检测泄漏
//
// 用法:
//   perf::MemoryTracker::instance().enable();
//   // ... 执行你的代码 ...
//   perf::MemoryTracker::instance().report();
// ============================================================
class MemoryTracker {
public:
    static MemoryTracker& instance() {
        static MemoryTracker inst;
        return inst;
    }

    void enable()  { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool isEnabled() const { return enabled_; }

    void recordAlloc(void* ptr, size_t size, const char* file, int line) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        allocations_[ptr] = {size, file ? file : "unknown", line};
        totalAllocated_ += size;
        currentUsage_ += size;
        if (currentUsage_ > peakUsage_) peakUsage_ = currentUsage_;
        allocCount_++;
    }

    void recordFree(void* ptr) {
        if (!enabled_ || !ptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = allocations_.find(ptr);
        if (it != allocations_.end()) {
            currentUsage_ -= it->second.size;
            allocations_.erase(it);
            freeCount_++;
        }
    }

    void report() const {
        std::cout << "\n============ Memory Report ============\n";
        std::cout << "  Total allocations:  " << allocCount_ << "\n";
        std::cout << "  Total frees:        " << freeCount_ << "\n";
        std::cout << "  Total allocated:    " << totalAllocated_ << " bytes\n";
        std::cout << "  Current usage:      " << currentUsage_ << " bytes\n";
        std::cout << "  Peak usage:         " << peakUsage_ << " bytes\n";

        if (!allocations_.empty()) {
            std::cout << "\n  *** LEAKS DETECTED: " << allocations_.size() << " ***\n";
            int count = 0;
            for (auto& [ptr, info] : allocations_) {
                std::cout << "    Leak: " << info.size << " bytes at "
                          << info.file << ":" << info.line
                          << " (ptr=" << ptr << ")\n";
                if (++count >= 20) {
                    std::cout << "    ... and " << (allocations_.size() - 20) << " more\n";
                    break;
                }
            }
        } else {
            std::cout << "\n  No leaks detected!\n";
        }
        std::cout << "=======================================\n\n";
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        allocations_.clear();
        totalAllocated_ = 0;
        currentUsage_ = 0;
        peakUsage_ = 0;
        allocCount_ = 0;
        freeCount_ = 0;
    }

#ifdef _WIN32
    // Windows 平台: 获取当前进程实际内存使用
    static void printProcessMemory() {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            std::cout << "[Process Memory]\n"
                      << "  Working Set:     " << pmc.WorkingSetSize / 1024 << " KB\n"
                      << "  Peak Working Set: " << pmc.PeakWorkingSetSize / 1024 << " KB\n"
                      << "  Pagefile Usage:  " << pmc.PagefileUsage / 1024 << " KB\n";
        }
    }
#endif

private:
    MemoryTracker() = default;

    struct AllocInfo {
        size_t size;
        std::string file;
        int line;
    };

    bool enabled_ = false;
    std::mutex mutex_;
    std::unordered_map<void*, AllocInfo> allocations_;
    size_t totalAllocated_ = 0;
    size_t currentUsage_ = 0;
    size_t peakUsage_ = 0;
    size_t allocCount_ = 0;
    size_t freeCount_ = 0;
};

// ============================================================
// 跟踪宏 - 用宏包裹 new/delete 方便跟踪分配位置
// ============================================================
#define TRACKED_NEW(T, ...) \
    [&]() { \
        T* p = new T(__VA_ARGS__); \
        perf::MemoryTracker::instance().recordAlloc(p, sizeof(T), __FILE__, __LINE__); \
        return p; \
    }()

#define TRACKED_NEW_ARRAY(T, count) \
    [&]() { \
        T* p = new T[count]; \
        perf::MemoryTracker::instance().recordAlloc(p, sizeof(T) * (count), __FILE__, __LINE__); \
        return p; \
    }()

#define TRACKED_DELETE(ptr) \
    do { \
        perf::MemoryTracker::instance().recordFree(ptr); \
        delete ptr; \
    } while(0)

#define TRACKED_DELETE_ARRAY(ptr) \
    do { \
        perf::MemoryTracker::instance().recordFree(ptr); \
        delete[] ptr; \
    } while(0)

} // namespace perf
