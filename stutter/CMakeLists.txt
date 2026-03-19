add_executable(stutter_cache_miss      01_cache_miss.cpp)
add_executable(stutter_false_sharing   02_false_sharing.cpp)
add_executable(stutter_lock_contention 03_lock_contention.cpp)
add_executable(stutter_blocking_io     04_blocking_io.cpp)

target_link_libraries(stutter_cache_miss      perf_common)
target_link_libraries(stutter_false_sharing   perf_common)
target_link_libraries(stutter_lock_contention perf_common)
target_link_libraries(stutter_blocking_io     perf_common)
