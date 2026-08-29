cmake_minimum_required(VERSION 3.16)

project(hedgehog_v2 VERSION 1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_VERBOSE_MAKEFILE ON)

if (MSVC)
    add_definitions(-DNOMINMAX)
endif (MSVC)

include(FetchContent) # Used to download GTEST

option(TEST_HH "Downloads google unit test API and runs google test scripts to test Hedgehog core and api" OFF)

set(CACHE_LINE_SIZE 64)

find_package(Threads REQUIRED)
find_package(CUDAToolkit QUIET)

link_libraries(Threads::Threads)

# TODO: don't need this right now
# configure_file(inputs_cmake/version.h.in "${PROJECT_SOURCE_DIR}/hedgehog/version.h")
# configure_file(inputs_cmake/constants.h.in "${PROJECT_SOURCE_DIR}/hedgehog/constants.h")

if (TEST_HH)
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

    if (MSVC)
        target_compile_options(gtest_hedgehog PRIVATE /bigobj)
    endif (MSVC)

    target_link_libraries(gtest_hedgehog gtest_main)

    include(GoogleTest)
    gtest_discover_tests(gtest_hedgehog)
endif (TEST_HH)


# Install the target
# install(TARGETS hedgehog EXPORT hedgehogTargets)

# TODO: figure out the install later (not sure it is really needed except for cmake stuff

# configure_file(
#         "${CMAKE_CURRENT_SOURCE_DIR}/hedgehogConfig.cmake.in"
#         "${CMAKE_CURRENT_BINARY_DIR}/hedgehogConfig.cmake"
#         @ONLY
# )

# install(
#         FILES
#         "${CMAKE_CURRENT_BINARY_DIR}/hedgehogConfig.cmake"
#         DESTINATION
#         "${CMAKE_INSTALL_PREFIX}/share/hedgehog"
# )

# install(
#         EXPORT
#         hedgehogTargets
#         NAMESPACE
#         hedgehog::
#         DESTINATION
#         "${CMAKE_INSTALL_PREFIX}/share/hedgehog"
#         FILE hedgehogTargets.cmake
# )


# install(DIRECTORY hedgehog
#         DESTINATION include
#         COMPONENT headers)
