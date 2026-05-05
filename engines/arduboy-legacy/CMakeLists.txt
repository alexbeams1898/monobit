## CMake build for the SDL (PC) target only. The Arduboy target keeps its
## existing Makefile — this file does NOT touch avr-gcc or atmega builds.
##
## Usage from an MSYS2 MinGW64 shell:
##   cmake -S . -B build-sdl -G Ninja -DCMAKE_BUILD_TYPE=Release
##   cmake --build build-sdl
##   ./build-sdl/bin/mono-sdl.exe
##
## The top-level Makefile wraps these with `make sdl` / `make sdl-run`.

cmake_minimum_required(VERSION 3.25)
project(mono-sdl VERSION 0.1.0 LANGUAGES C CXX)

# FetchContent on old CMake-policy dependencies needs this on CMake 4+.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json for editor IntelliSense and clang-tidy.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Predictable output locations.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# SDL2 via FetchContent. Static link produces a self-contained .exe (no
# runtime DLL hunting). Same approach used in the sister project
# prison-break-game — proven on this machine with MSYS2 MinGW64.
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
# SDL2 2.30.9 declares an old cmake_minimum_required — suppress the
# deprecation warning just for its subtree.
set(CMAKE_WARN_DEPRECATED FALSE CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)
set(CMAKE_WARN_DEPRECATED TRUE CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# Source lists. Keep parallel with the Makefile so both builds stay in
# step — adding a file to engine/ or games/rpg/ should flow naturally to
# both targets. platform/sdl/ is the per-platform swap.
# ---------------------------------------------------------------------------
file(GLOB ENGINE_SRCS   CONFIGURE_DEPENDS engine/*.cpp)
file(GLOB GAME_SRCS     CONFIGURE_DEPENDS games/rpg/*.cpp)
file(GLOB PLATFORM_SRCS CONFIGURE_DEPENDS platform/sdl/*.cpp)

add_executable(mono-sdl
    ${ENGINE_SRCS}
    ${GAME_SRCS}
    ${PLATFORM_SRCS}
)

target_include_directories(mono-sdl PRIVATE
    engine
    platform/sdl
    games/rpg
    ${CMAKE_SOURCE_DIR}/build/fxdata
)

# Generate the FX data image + offset header before compiling. The header
# (data_offsets.h) is included by game source; data.bin is copied next to
# the SDL exe by the top-level Makefile rule.
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/build/fxdata/data_offsets.h
           ${CMAKE_SOURCE_DIR}/build/fxdata/data.bin
    COMMAND ${CMAKE_COMMAND} -E env python ${CMAKE_SOURCE_DIR}/tools/fxdata/build.py
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/fxdata/manifest.txt
            ${CMAKE_SOURCE_DIR}/tools/fxdata/build.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating FX data image"
)
add_custom_target(fxdata DEPENDS ${CMAKE_SOURCE_DIR}/build/fxdata/data_offsets.h)
add_dependencies(mono-sdl fxdata)

target_link_libraries(mono-sdl PRIVATE SDL2::SDL2-static SDL2::SDL2main)

# Always-on PC-only dev-test features: CLI overrides, cheat-mode spawn,
# state jumping. Zero Arduboy impact; the function that implements them
# doesn't exist in the AVR build.
target_compile_definitions(mono-sdl PRIVATE ENABLE_DEBUG_OVERRIDES)

# Warnings as errors matches the Arduboy Makefile's -Wall -Wextra. We
# don't use -Werror because FetchContent_MakeAvailable doesn't let us
# isolate warning flags from SDL2's own headers cleanly.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(mono-sdl PRIVATE -Wall -Wextra)
endif()

# Optional: define PERF_UART_LOG to enable stdout frame-time logging for
# tools/perf_logger. Off by default; flip on with -DPERF_UART_LOG=ON.
option(PERF_UART_LOG "Emit per-frame state+us bytes to stdout" OFF)
if(PERF_UART_LOG)
    target_compile_definitions(mono-sdl PRIVATE PERF_UART_LOG DEBUG)
endif()

# Optional: define DBG_TRACE to enable engine/dbg.h tracepoints. Off
# by default; disabled builds carry zero cost (empty inline stubs).
# Flip on with -DDBG_TRACE=ON. See tools/dbg_trace/README.md.
option(DBG_TRACE "Enable engine/dbg.h tracepoints" OFF)
if(DBG_TRACE)
    target_compile_definitions(mono-sdl PRIVATE DBG_TRACE)
endif()

# ---------------------------------------------------------------------------
# vestigia_smoke — non-interactive driver that exercises the save protocol
# end-to-end (init / read / write / erase / round-trip) against the SDL
# data_flash and storage backends. Used to bisect platform-shared bugs
# from Arduboy-specific bugs without needing a UI loop.
# ---------------------------------------------------------------------------
add_executable(vestigia_smoke
    tests/vestigia_smoke.cpp
    games/rpg/vestigia.cpp
    platform/sdl/data_flash.cpp
    platform/sdl/storage.cpp
    platform/sdl/audio.cpp
    engine/audio.cpp
)
target_include_directories(vestigia_smoke PRIVATE
    engine
    platform/sdl
    games/rpg
    ${CMAKE_SOURCE_DIR}/build/fxdata
)
target_link_libraries(vestigia_smoke PRIVATE SDL2::SDL2-static SDL2::SDL2main)
add_dependencies(vestigia_smoke fxdata)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(vestigia_smoke PRIVATE -Wall -Wextra)
endif()
