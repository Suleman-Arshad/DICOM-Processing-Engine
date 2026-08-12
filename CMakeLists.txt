cmake_minimum_required(VERSION 3.20)
project(DicomProcessor
    VERSION 1.0.0
    DESCRIPTION "Multi-threaded C++20 DICOM Image Processing Pipeline"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)
find_package(PNG REQUIRED)

# --- Ingestion library (Week 5) ---------------------------------------------
add_library(dicom_ingestion STATIC
    src/vr.cpp
    src/dictionary.cpp
    src/dataset.cpp
    src/parser.cpp
)
target_include_directories(dicom_ingestion PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# NOTE: AVX2 flags intentionally NOT applied here or at any library level
# except filters_avx2.cpp -- see that file for why (SIGILL on non-AVX2
# hardware if applied broadly).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(dicom_ingestion PUBLIC -Wall -Wextra -Wpedantic)
endif()

# --- Reconstruction library (Week 6-7: volume + thread pool) ----------------
add_library(dicom_reconstruction STATIC
    src/voxel_volume.cpp
    src/reconstruction.cpp
    src/thread_pool.cpp
)
target_link_libraries(dicom_reconstruction PUBLIC dicom_ingestion Threads::Threads)

# --- Detection library (Week 7) ---------------------------------------------
add_library(dicom_detection STATIC
    src/detection.cpp
)
target_link_libraries(dicom_detection PUBLIC dicom_reconstruction)

# --- Processing library (Week 6: SIMD filters) ------------------------------
add_library(dicom_processing STATIC
    src/filters_scalar.cpp
    src/filters_avx2.cpp
    src/filters_dispatch.cpp
)
target_link_libraries(dicom_processing PUBLIC dicom_reconstruction)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(src/filters_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set_source_files_properties(src/filters_avx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
endif()

# --- Output library (Week 8) -------------------------------------------------
# JSON findings report + optional FHIR export, PNG slice export (libpng),
# and annotated DICOM re-encoding.
add_library(dicom_output STATIC
    src/json_writer.cpp
    src/findings_report.cpp
    src/png_writer.cpp
    src/dicom_writer.cpp
)
target_link_libraries(dicom_output PUBLIC dicom_detection PNG::PNG)

# --- Executables -------------------------------------------------------------
add_executable(dicom_processor src/main.cpp)
target_link_libraries(dicom_processor PRIVATE dicom_ingestion)

add_executable(volume_reconstructor src/reconstruct_main.cpp)
target_link_libraries(volume_reconstructor PRIVATE dicom_reconstruction)

add_executable(anomaly_detector src/detect_main.cpp)
target_link_libraries(anomaly_detector PRIVATE dicom_detection)

add_executable(filter_benchmark src/benchmark_main.cpp)
target_link_libraries(filter_benchmark PRIVATE dicom_processing)

add_executable(threadpool_benchmark src/threadpool_benchmark_main.cpp)
target_link_libraries(threadpool_benchmark PRIVATE dicom_detection)

add_executable(pipeline_export src/pipeline_export.cpp)
target_link_libraries(pipeline_export PRIVATE dicom_output)

# --- Testing (enable with -DBUILD_TESTING=ON) --------------------------------
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
