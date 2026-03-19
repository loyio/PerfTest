add_executable(memory_leak_basic      01_basic_leak.cpp)
add_executable(memory_leak_smart_ptr   02_smart_ptr_fix.cpp)
add_executable(memory_leak_circular    03_circular_reference.cpp)
add_executable(memory_leak_win_api     04_win_api_leak.cpp)

target_link_libraries(memory_leak_basic      perf_common)
target_link_libraries(memory_leak_smart_ptr  perf_common)
target_link_libraries(memory_leak_circular   perf_common)
target_link_libraries(memory_leak_win_api    perf_common)
