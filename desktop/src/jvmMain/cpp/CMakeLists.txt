cmake_minimum_required(VERSION 3.22)
project(tap_looper_bridge_desktop CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# On macOS with Command Line Tools, /usr/bin/clang++ does not auto-include
# the libc++ headers — adding them explicitly via xcrun (same fix as engine).
if(APPLE AND NOT ANDROID)
    execute_process(COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE TL_MACOS_SDK_PATH
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE TL_XCRUN_RESULT)
    if(TL_XCRUN_RESULT EQUAL 0 AND EXISTS "${TL_MACOS_SDK_PATH}/usr/include/c++/v1")
        add_compile_options(-isystem "${TL_MACOS_SDK_PATH}/usr/include/c++/v1")
    endif()
endif()

# Pull in the portable engine
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine
                 ${CMAKE_BINARY_DIR}/engine)

find_package(JNI REQUIRED)

# miniaudio + bridge as a single shared lib
add_library(tap_looper_bridge SHARED
    audio_io_miniaudio.cpp
    bridge.cpp)

target_include_directories(tap_looper_bridge PRIVATE
    ${JNI_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine/src/platform)

target_link_libraries(tap_looper_bridge PRIVATE tl_engine)

if(APPLE)
    target_link_libraries(tap_looper_bridge PRIVATE "-framework CoreAudio" "-framework AudioUnit" "-framework CoreFoundation")
endif()

set_target_properties(tap_looper_bridge PROPERTIES
    OUTPUT_NAME "tap_looper_bridge"
    POSITION_INDEPENDENT_CODE ON)
