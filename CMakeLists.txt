cmake_minimum_required(VERSION 3.16)

project(hedgehog_v2 VERSION 1 LANGUAGES CXX)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CMAKE_CXX_STANDARD 20)
endif ()

set(CMAKE_VERBOSE_MAKEFILE ON)

if (MSVC)
    add_definitions(-DNOMINMAX)
endif (MSVC)

include(FetchContent)

option(HH_TEST "Downloads google unit test API and runs google test scripts to test Hedgehog core and api" OFF)
option(HH_TEST_META "Test meta-functions" OFF)
option(HH_ENABLE_PROFILING "Enable profiling." OFF)
option(HH_ENABLE_CONCEPTS "Enable concept constraints on template parameters (slower compile)" OFF)
option(HH_ENABLE_TBB "Enable TBB-based executor (fetched via FetchContent if not found)" OFF)
option(ENABLE_NVTX "Enable NVTX if CUDA is found" OFF)

set(CACHE_LINE_SIZE 64)

find_package(Threads REQUIRED)
find_package(CUDAToolkit QUIET)

# Header-only INTERFACE library target
add_library(hedgehog INTERFACE)
add_library(hedgehog::hedgehog ALIAS hedgehog)

target_include_directories(
    hedgehog
    INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(hedgehog INTERFACE cxx_std_20)
target_link_libraries(hedgehog INTERFACE Threads::Threads)

if (HH_ENABLE_TBB)
    find_package(TBB QUIET)
    if (NOT TBB_FOUND)
        message(STATUS "TBB not found on system, fetching via FetchContent...")
        set(BUILD_SHARED_LIBS ON)
        set(TBB_TEST OFF CACHE BOOL "" FORCE)
        set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(TBB_STRICT OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            onetbb
            GIT_REPOSITORY https://github.com/uxlfoundation/oneTBB.git
            GIT_TAG        v2023.1.0
            GIT_SHALLOW    TRUE
            OVERRIDE_FIND_PACKAGE
        )
        FetchContent_MakeAvailable(onetbb)
        if (TARGET tbb AND NOT TARGET TBB::tbb)
            add_library(TBB::tbb ALIAS tbb)
        endif()
        unset(BUILD_SHARED_LIBS)
    else()
        message(STATUS "Using system TBB: ${TBB_DIR}")
    endif()
    target_link_libraries(hedgehog INTERFACE TBB::tbb)
    target_compile_definitions(hedgehog INTERFACE HH_ENABLE_TBB)
endif()

if (HH_ENABLE_PROFILING)
    target_compile_definitions(hedgehog INTERFACE HH_ENABLE_PROFILING)
endif()

if (HH_ENABLE_CONCEPTS)
    target_compile_definitions(hedgehog INTERFACE HH_ENABLE_CONCEPTS)
endif()

if (CUDAToolkit_FOUND)
    message("CUDA found: ${CUDAToolkit_INCLUDE_DIRS}")
    target_link_libraries(hedgehog INTERFACE CUDA::cudart CUDA::cuda_driver CUDA::cupti)
    target_compile_definitions(hedgehog INTERFACE HH_USE_CUDA)
    if (ENABLE_NVTX)
        if (TARGET CUDA::nvtx3)
            target_link_libraries(hedgehog INTERFACE CUDA::nvtx3)
        elseif (TARGET CUDA::nvToolsExt)
            target_link_libraries(hedgehog INTERFACE CUDA::nvToolsExt)
        endif ()
        if (NOT WIN32)
            target_link_libraries(hedgehog INTERFACE dl)
        endif ()
        target_compile_definitions(hedgehog INTERFACE HH_USE_NVTX)
    endif (ENABLE_NVTX)
else (CUDAToolkit_FOUND)
    message("CUDA not found, all features will not be available.")
endif (CUDAToolkit_FOUND)

# Compiler warnings only when building standalone (not via FetchContent)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_compile_options(-Wall -Wextra -Wno-unused-local-typedefs)
endif ()

if (HH_TEST)
    if (HH_TEST_META)
        target_compile_definitions(hedgehog INTERFACE HH_TEST_META)
    endif(HH_TEST_META)
    message("Fetch Gtest")
    if (POLICY CMP0135)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
            DOWNLOAD_EXTRACT_TIMESTAMP true
        )
    else ()
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
        )
    endif ()

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    file(GLOB_RECURSE test_hedgehog_header tests/*.h)
    add_executable(gtest_hedgehog ${test_hedgehog_header} tests/test_hedgehog.cpp)
    add_executable(gtest_allocators ${test_hedgehog_header} tests/test_allocators.cpp)

    if (MSVC)
        target_compile_options(gtest_hedgehog PRIVATE /bigobj)
        target_compile_options(gtest_allocators PRIVATE /bigobj)
    endif (MSVC)

    target_link_libraries(gtest_hedgehog gtest_main hedgehog)
    target_link_libraries(gtest_allocators gtest_main hedgehog)

    include(GoogleTest)
    gtest_discover_tests(gtest_hedgehog)
    gtest_discover_tests(gtest_allocators)
endif (HH_TEST)

# Install
install(TARGETS hedgehog EXPORT hedgehogTargets)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/hedgehogConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/hedgehogConfig.cmake"
    @ONLY
)

install(
    FILES
    "${CMAKE_CURRENT_BINARY_DIR}/hedgehogConfig.cmake"
    DESTINATION
    "${CMAKE_INSTALL_PREFIX}/share/hedgehog"
)

install(
    EXPORT
    hedgehogTargets
    NAMESPACE
    hedgehog::
    DESTINATION
    "${CMAKE_INSTALL_PREFIX}/share/hedgehog"
    FILE hedgehogTargets.cmake
)

install(DIRECTORY hedgehog
    DESTINATION include
    COMPONENT headers)
