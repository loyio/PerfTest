add_executable(framerate_game_loop    01_game_loop.cpp)
add_executable(framerate_frame_pacing 02_frame_pacing.cpp)
add_executable(framerate_object_pool  03_object_pooling.cpp)

target_link_libraries(framerate_game_loop    perf_common)
target_link_libraries(framerate_frame_pacing perf_common winmm)
target_link_libraries(framerate_object_pool  perf_common)
