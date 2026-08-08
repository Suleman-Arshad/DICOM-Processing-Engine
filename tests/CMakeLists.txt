# tests/CMakeLists.txt — enabled via -DBUILD_TESTING=ON.
# Correctness tests, not benchmarks (those are src/*_benchmark_main.cpp).
# Run with `ctest` from the build directory, or execute binaries directly.

add_executable(thread_pool_test thread_pool_test.cpp)
target_link_libraries(thread_pool_test PRIVATE dicom_reconstruction)
add_test(NAME thread_pool_test COMMAND thread_pool_test)

add_executable(reconstruct_parallel_test reconstruct_parallel_test.cpp)
target_link_libraries(reconstruct_parallel_test PRIVATE dicom_reconstruction)
add_test(NAME reconstruct_parallel_test COMMAND reconstruct_parallel_test)

add_executable(detection_test detection_test.cpp)
target_link_libraries(detection_test PRIVATE dicom_detection)
add_test(NAME detection_test COMMAND detection_test)

add_executable(detection_parallel_test detection_parallel_test.cpp)
target_link_libraries(detection_parallel_test PRIVATE dicom_detection)
add_test(NAME detection_parallel_test COMMAND detection_parallel_test)
