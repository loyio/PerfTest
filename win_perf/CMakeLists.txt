add_executable(win_perf_etw           01_etw_tracing.cpp)
add_executable(win_perf_query_counter 02_query_perf_counter.cpp)

target_link_libraries(win_perf_etw           perf_common advapi32)
target_link_libraries(win_perf_query_counter perf_common winmm)
