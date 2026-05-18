cmake_minimum_required(VERSION 3.22)
project(tap_looper_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# On macOS with Command Line Tools, /usr/bin/clang++ does not auto-include
# the libc++ headers — adding them explicitly via xcrun.
if(APPLE AND NOT ANDROID)
    execute_process(COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE TL_MACOS_SDK_PATH
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE TL_XCRUN_RESULT)
    if(TL_XCRUN_RESULT EQUAL 0 AND EXISTS "${TL_MACOS_SDK_PATH}/usr/include/c++/v1")
        add_compile_options(-isystem "${TL_MACOS_SDK_PATH}/usr/include/c++/v1")
    endif()
endif()

option(TL_BUILD_TESTS "Build engine gtest suite" ON)

set(TL_ENGINE_SOURCES
    src/state_snapshot.cpp
    src/sample.cpp
    src/rolling_buffer.cpp
    src/bpm.cpp
    src/mixer.cpp
    src/audio_engine.cpp
)

add_library(tl_engine STATIC ${TL_ENGINE_SOURCES})
target_include_directories(tl_engine PUBLIC src)
target_compile_options(tl_engine PRIVATE -Wall -Wextra -Werror)

if(TL_BUILD_TESTS AND NOT ANDROID)
    include(FetchContent)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    file(GLOB TL_TEST_SOURCES test/*_test.cpp)
    add_executable(tl_engine_tests ${TL_TEST_SOURCES})
    target_link_libraries(tl_engine_tests PRIVATE tl_engine gtest_main)
    include(GoogleTest)
    gtest_discover_tests(tl_engine_tests)
endif()
