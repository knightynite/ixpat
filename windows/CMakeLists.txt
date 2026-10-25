cmake_minimum_required(VERSION 3.26)
project(iXpatWindows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# One binary, no redistributable: static CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor /external:W0)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# Two extra passes, off by default (build.ps1 -Analyze / -Sanitize): the MSVC static analyser over our own sources
# only, and AddressSanitizer for the test run. Either one changes the whole build, so expect a full rebuild.
option(IXPAT_ANALYZE "Compile with /analyze (warnings are errors like the rest)" OFF)
option(IXPAT_ASAN "Build with AddressSanitizer" OFF)
if(IXPAT_ANALYZE)
  add_compile_options(/analyze /analyze:external- /external:anglebrackets)
endif()
if(IXPAT_ASAN)
  add_compile_options(/fsanitize=address /Zi)
  string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")  # incompatible with the sanitizer
endif()

# Platform-free engine: no <windows.h> anywhere under src/core.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(ixpat_core STATIC ${CORE_SOURCES})
target_include_directories(ixpat_core PUBLIC src)
target_include_directories(ixpat_core SYSTEM PUBLIC third_party)

# Win32 glue: one responsibility per file, thin, no engine logic.
file(GLOB WIN_SOURCES CONFIGURE_DEPENDS src/win/*.cpp)
add_library(ixpat_win STATIC ${WIN_SOURCES})
target_link_libraries(ixpat_win PUBLIC ixpat_core user32 shell32 ole32 oleaut32 wbemuuid uiautomationcore winhttp comctl32 hid cfgmgr32 ws2_32 dwmapi winmm dbghelp wintrust crypt32)

# Direct2D UI: foundation, theme, ring window, settings window.
file(GLOB UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)
add_library(ixpat_ui STATIC ${UI_SOURCES})
target_link_libraries(ixpat_ui PUBLIC ixpat_core d2d1 dwrite gdi32 user32 shcore dwmapi comdlg32 comctl32)

# Unit tests (doctest).
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(ixpat_tests ${TEST_SOURCES})
target_link_libraries(ixpat_tests PRIVATE ixpat_core ixpat_win ixpat_ui)
target_compile_definitions(ixpat_tests PRIVATE IXPAT_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
enable_testing()
add_test(NAME core COMMAND ixpat_tests)

# ixpat-probe: the hardware instrument (console tool).
file(GLOB PROBE_SOURCES CONFIGURE_DEPENDS tools/probe/*.cpp)
add_executable(ixpat_probe ${PROBE_SOURCES})
target_include_directories(ixpat_probe PRIVATE tools/probe)
target_link_libraries(ixpat_probe PRIVATE ixpat_core hid cfgmgr32 user32)
set_target_properties(ixpat_probe PROPERTIES OUTPUT_NAME ixpat-probe)

# iXpat.exe: the tray application.
add_executable(ixpat WIN32 src/app/main.cpp res/ixpat.rc res/app.manifest)
target_include_directories(ixpat PRIVATE src)
target_link_libraries(ixpat PRIVATE ixpat_core ixpat_win ixpat_ui)
set_target_properties(ixpat PROPERTIES OUTPUT_NAME iXpat)
