add_library(perf_common STATIC
    profiler.cpp
    memory_tracker.cpp
)
target_include_directories(perf_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
