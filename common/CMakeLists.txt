add_library(perf_common STATIC
    profiler.cpp
    memory_tracker.cpp
    telemetry.cpp
)
target_include_directories(perf_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Tracy Profiler 集成
if(PERF_ENABLE_TRACY)
    target_link_libraries(perf_common PUBLIC TracyClient)
endif()

# 生产 Telemetry 系统
if(PERF_ENABLE_TELEMETRY)
    target_compile_definitions(perf_common PUBLIC PERF_TELEMETRY_ENABLE)
    message(STATUS "Production Telemetry ENABLED")
endif()
