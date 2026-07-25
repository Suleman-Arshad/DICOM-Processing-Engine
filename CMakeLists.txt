cmake_minimum_required(VERSION 3.20)
project(DicomProcessor
    VERSION 0.1.0
    DESCRIPTION "Multi-threaded C++20 DICOM Image Processing Pipeline"
    LANGUAGES CXX
)

# --- Language standard ---------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # needed by clangd / editor tooling

# --- Build type default ---------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# --- Ingestion library ------------------------------------------------------
add_library(dicom_ingestion STATIC
    src/vr.cpp
    src/dictionary.cpp
    src/dataset.cpp
    src/parser.cpp
)
target_include_directories(dicom_ingestion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(dicom_ingestion PUBLIC -Wall -Wextra -Wpedantic)
endif()

# --- Main executable ---------------------------------------------------
add_executable(dicom_processor src/main.cpp)
target_link_libraries(dicom_processor PRIVATE dicom_ingestion)

# --- Optional DCMTK cross-verification tool -----------------------------
option(BUILD_DCMTK_VERIFY_TOOL "Build the optional DCMTK cross-verification tool" ON)
if(BUILD_DCMTK_VERIFY_TOOL)
    find_package(DCMTK REQUIRED)
    add_executable(dcmtk_verify tools/dcmtk_verify.cpp)
    target_link_libraries(dcmtk_verify PRIVATE DCMTK::DCMTK)
endif()

# --- Testing scaffold (enable later with -DBUILD_TESTING=ON) -----------
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()