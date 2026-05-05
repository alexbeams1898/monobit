# Ardens-headless smoke harness for Arduboy builds.
#
# Vendors absim (Ardens' simulation core) via the pinned `tools/ardens`
# submodule, builds it as a static lib without GUI / LLVM / SDL deps,
# and links our `main.cpp` driver against it. Exits 0 on a clean N-
# second run; exits 1 if any of the five autobreak signals fired.
#
# Build: from this directory,
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
# Output: build/ardens_smoke[.exe]
#
# Top-level Makefile invokes this via the `ardens-smoke` target;
# .github/workflows/ci.yml runs it on every PR.

cmake_minimum_required(VERSION 3.16)

project(ardens_smoke CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# The pinned Ardens submodule lives one level up.
set(ARDENS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ardens")
if(NOT EXISTS "${ARDENS_DIR}/src/absim.hpp")
    message(FATAL_ERROR
        "Ardens submodule missing at ${ARDENS_DIR}. "
        "Run: git submodule update --init tools/ardens "
        "&& cd tools/ardens && git submodule update --init deps/miniz deps/fmt")
endif()

# ---- fmt (header-only mode is fine for absim's usage) ----
# Use fmt as a compiled lib for simpler linkage.
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${ARDENS_DIR}/deps/fmt" "${CMAKE_BINARY_DIR}/_deps/fmt"
    EXCLUDE_FROM_ALL)

# ---- miniz (.arduboy zip parsing — absim_load_file.cpp includes its
#       headers unconditionally, even though we never call any zip
#       functions because we only feed plain .hex). Build the C source
#       directly. ----
add_library(ardens_smoke_miniz STATIC
    "${ARDENS_DIR}/deps/miniz/miniz.c"
    "${ARDENS_DIR}/deps/miniz/miniz_tdef.c"
    "${ARDENS_DIR}/deps/miniz/miniz_tinfl.c"
    "${ARDENS_DIR}/deps/miniz/miniz_zip.c"
)
# `${CMAKE_CURRENT_SOURCE_DIR}` carries our miniz_export.h stub
# (see comments in that file for why we don't run miniz's CMake).
target_include_directories(ardens_smoke_miniz PUBLIC
    "${ARDENS_DIR}/deps/miniz"
    "${CMAKE_CURRENT_SOURCE_DIR}")

# ---- yyjson (JSON for .arduboy metadata). Vendored under deps/yyjson
#       in the Ardens repo as plain source. ----
add_library(ardens_smoke_yyjson STATIC
    "${ARDENS_DIR}/deps/yyjson/yyjson.c"
)
target_include_directories(ardens_smoke_yyjson PUBLIC
    "${ARDENS_DIR}/deps")

# ---- bitsery (header-only C++ serialization, used by absim_snapshot) ----
# Lives at deps/bitsery/include — header-only. Add include dir below.

# ---- absim core ----
# Subset of Ardens' ARDENS_SOURCES that we need. Excludes anything
# that pulls in LLVM / GUI / sokol / imgui.
set(ABSIM_SOURCES
    "${ARDENS_DIR}/src/absim.hpp"
    "${ARDENS_DIR}/src/absim_arduboy.cpp"
    "${ARDENS_DIR}/src/absim_cpu_data.cpp"
    "${ARDENS_DIR}/src/absim_decode.cpp"
    "${ARDENS_DIR}/src/absim_disassemble.cpp"
    "${ARDENS_DIR}/src/absim_dwarf.cpp"
    "${ARDENS_DIR}/src/absim_dwarf_expr.cpp"
    "${ARDENS_DIR}/src/absim_execute.cpp"
    "${ARDENS_DIR}/src/absim_led.cpp"
    "${ARDENS_DIR}/src/absim_load_file.cpp"
    "${ARDENS_DIR}/src/absim_merge_instrs.cpp"
    "${ARDENS_DIR}/src/absim_reset.cpp"
    "${ARDENS_DIR}/src/absim_snapshot.cpp"
)
file(GLOB ABSIM_BOOT "${ARDENS_DIR}/src/boot/*.c"
                     "${ARDENS_DIR}/src/boot/*.h")
list(APPEND ABSIM_SOURCES ${ABSIM_BOOT})

add_library(ardens_smoke_absim STATIC ${ABSIM_SOURCES})

target_include_directories(ardens_smoke_absim PUBLIC
    "${ARDENS_DIR}/src"
    "${ARDENS_DIR}/deps"
    "${ARDENS_DIR}/deps/bitsery/include"
)

# Mirror the ARDENS_PLAYER preprocessor flags from Ardens'
# CMakeLists.txt, but DO NOT define ARDENS_NO_DEBUGGER — we want the
# autobreak-tracking machinery active. Skip ARDENS_LLVM so the DWARF
# code paths in absim_load_file.cpp / absim_dwarf*.cpp are stubbed.
target_compile_definitions(ardens_smoke_absim PUBLIC
    ARDENS_PLAYER
    ARDENS_NO_SNAPSHOTS
    ARDENS_NO_GUI
    ARDENS_NO_SAVED_SETTINGS
    ARDENS_NO_SCALING
    # Version macros are normally injected by upstream Ardens'
    # top-level CMake; absim_snapshot.cpp references them
    # unconditionally. Pin to the submodule version so the values
    # baked into snapshot blobs match upstream's.
    ARDENS_VERSION_MAJOR=0
    ARDENS_VERSION_MINOR=24
    ARDENS_VERSION_PATCH=15
)

# Suppress upstream warnings — we don't own this code.
if(MSVC)
    target_compile_options(ardens_smoke_absim PRIVATE /w)
else()
    target_compile_options(ardens_smoke_absim PRIVATE -w)
endif()

target_link_libraries(ardens_smoke_absim PUBLIC
    ardens_smoke_miniz
    ardens_smoke_yyjson
    fmt::fmt
)

# ---- Smoke harness exe ----
add_executable(ardens_smoke main.cpp)
target_link_libraries(ardens_smoke PRIVATE ardens_smoke_absim)

# Optimize for size — this exe is a CI gate, not perf-critical.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Static-link runtime on Windows so the binary is portable across CI
# images without depending on a specific MSVCR.
if(MINGW)
    target_link_options(ardens_smoke PRIVATE -static-libgcc -static-libstdc++)
endif()
