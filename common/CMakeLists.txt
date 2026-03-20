add_library(perf_common STATIC
    profiler.cpp
    memory_tracker.cpp
)
target_include_directories(perf_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Tracy Profiler 集成
if(PERF_ENABLE_TRACY)
    target_link_libraries(perf_common PUBLIC TracyClient)
endif()
