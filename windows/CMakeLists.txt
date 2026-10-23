cmake_minimum_required(VERSION 3.26)
project(iXpatWindows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# One binary, no redistributable: static CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor /external:W0)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# Platform-free engine: no <windows.h> anywhere under src/core.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(ixpat_core STATIC ${CORE_SOURCES})
target_include_directories(ixpat_core PUBLIC src)
target_include_directories(ixpat_core SYSTEM PUBLIC third_party)

# Unit tests (doctest).
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(ixpat_tests ${TEST_SOURCES})
target_link_libraries(ixpat_tests PRIVATE ixpat_core)
target_compile_definitions(ixpat_tests PRIVATE IXPAT_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
enable_testing()
add_test(NAME core COMMAND ixpat_tests)

# ixpat-probe: the hardware instrument (console tool).
file(GLOB PROBE_SOURCES CONFIGURE_DEPENDS tools/probe/*.cpp)
add_executable(ixpat_probe ${PROBE_SOURCES})
target_include_directories(ixpat_probe PRIVATE tools/probe)
target_link_libraries(ixpat_probe PRIVATE ixpat_core hid cfgmgr32 user32)
set_target_properties(ixpat_probe PROPERTIES OUTPUT_NAME ixpat-probe)
