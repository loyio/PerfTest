/*
 * case_blocking_io.cpp - 阻塞 I/O 测试
 *
 * 基准模式: 对比不同文件写入/读取策略的性能差异。
 * 演示为什么主线程做 I/O 会导致卡顿。
 */

#include "perf_case.h"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdio>

#ifdef _WIN32
#include <Windows.h>
#endif

class BlockingIoCase : public PerfCase {
public:
    const char* getCategory()    const override { return "Stutter"; }
    const char* getName()        const override { return "Blocking I/O"; }
    const char* getDescription() const override {
        return "Compare per-line flush vs batched write,\n"
               "and different read methods (ifstream/fread/ReadFile).\n"
               "Shows why synchronous I/O on main thread causes stutter.";
    }

    void onDrawUI() override {
        ImGui::SliderInt("Lines to write", &writeLines_, 1000, 100000, "%d",
                         ImGuiSliderFlags_Logarithmic);

        if (ImGui::Button("Benchmark Write Strategies")) {
            benchmarkWrite();
        }
        if (writeDone_) {
            ImGui::Text("Per-line + flush: %.2f ms", writeFlushMs_);
            ImGui::Text("Per-line (buf):   %.2f ms", writeBufferedMs_);
            ImGui::Text("Batched:          %.2f ms", writeBatchMs_);
            float ratio = writeBatchMs_ > 0 ? writeFlushMs_ / writeBatchMs_ : 0;
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1),
                               "Flush penalty: %.1fx", ratio);
        }

        ImGui::Separator();

        if (ImGui::Button("Benchmark Read Methods")) {
            benchmarkRead();
        }
        if (readDone_) {
            ImGui::Text("std::ifstream: %.2f ms", readIfstreamMs_);
            ImGui::Text("C fread:       %.2f ms", readFreadMs_);
#ifdef _WIN32
            ImGui::Text("Win32 ReadFile: %.2f ms", readWin32Ms_);
#endif
        }

        // 每帧 I/O 压力
        ImGui::Separator();
        ImGui::Checkbox("Per-frame I/O (simulate log)", &perFrameIo_);
        if (perFrameIo_ && active) {
            ImGui::Text("Per-frame I/O cost: %.2f ms", lastUpdateMs);
        }
    }

    void onUpdate(float) override {
        if (!perFrameIo_) return;
        // 模拟每帧写日志
        std::ofstream f("_perf_log.tmp", std::ios::app);
        for (int i = 0; i < 100; i++) {
            f << "Frame log line " << i
              << " with some data: ABCDEFGHIJKLMNOP\n";
        }
        f.flush();
    }

    void onDeactivate() override {
        std::remove("_perf_io_test.tmp");
        std::remove("_perf_log.tmp");
    }

private:
    int  writeLines_ = 10000;
    bool perFrameIo_ = false;

    bool  writeDone_ = false;
    float writeFlushMs_ = 0, writeBufferedMs_ = 0, writeBatchMs_ = 0;

    bool  readDone_ = false;
    float readIfstreamMs_ = 0, readFreadMs_ = 0, readWin32Ms_ = 0;

    const std::string line_ = "Test log line with some data: 1234567890 ABCDEF\n";

    void benchmarkWrite() {
        constexpr int RUNS = 3;
        float f1 = 0, f2 = 0, f3 = 0;
        int lines = writeLines_;

        for (int r = 0; r < RUNS; r++) {
            f1 += measure([&]() {
                std::ofstream f("_perf_io_test.tmp");
                for (int i = 0; i < lines; i++) { f << line_; f.flush(); }
            });
            f2 += measure([&]() {
                std::ofstream f("_perf_io_test.tmp");
                for (int i = 0; i < lines; i++) { f << line_; }
            });
            f3 += measure([&]() {
                std::ostringstream ss;
                for (int i = 0; i < lines; i++) ss << line_;
                std::ofstream f("_perf_io_test.tmp");
                f << ss.str();
            });
        }
        writeFlushMs_    = f1 / RUNS;
        writeBufferedMs_ = f2 / RUNS;
        writeBatchMs_    = f3 / RUNS;
        writeDone_ = true;
        std::remove("_perf_io_test.tmp");
    }

    void benchmarkRead() {
        // Prepare test file (~5MB)
        {
            std::ofstream f("_perf_io_test.tmp", std::ios::binary);
            for (int i = 0; i < 100000; i++) f << line_;
        }

        constexpr int RUNS = 5;
        float f1 = 0, f2 = 0, f3 = 0;

        for (int r = 0; r < RUNS; r++) {
            // ifstream
            f1 += measure([&]() {
                std::ifstream f("_perf_io_test.tmp");
                std::string content((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
                volatile size_t sz = content.size();
                (void)sz;
            });

            // fread
            f2 += measure([&]() {
                FILE* fp = fopen("_perf_io_test.tmp", "rb");
                if (!fp) return;
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                std::vector<char> buf(sz);
                fread(buf.data(), 1, sz, fp);
                fclose(fp);
            });

#ifdef _WIN32
            // Win32 ReadFile
            f3 += measure([&]() {
                HANDLE h = CreateFileA("_perf_io_test.tmp", GENERIC_READ,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
                if (h == INVALID_HANDLE_VALUE) return;
                LARGE_INTEGER li;
                GetFileSizeEx(h, &li);
                std::vector<char> buf(static_cast<size_t>(li.QuadPart));
                DWORD read = 0;
                ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
                CloseHandle(h);
            });
#endif
        }

        readIfstreamMs_ = f1 / RUNS;
        readFreadMs_    = f2 / RUNS;
        readWin32Ms_    = f3 / RUNS;
        readDone_ = true;
        std::remove("_perf_io_test.tmp");
    }
};

ADD_PERF_CASE(BlockingIoCase)
