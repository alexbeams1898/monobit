include(FetchContent)

# ---------------------------------------------------------------------------
# OpenGL  (system library — not fetched, just located)
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# GLAD  (OpenGL 3.3 core function loader)
# Generates glad.h + glad.c at configure time using Python.
# Requires Python 3 — install on MSYS2 with: pacman -S python
# ---------------------------------------------------------------------------
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v0.1.36
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(GLAD_PROFILE   "core" CACHE STRING "" FORCE)
set(GLAD_API       "gl=3.3" CACHE STRING "" FORCE)
set(GLAD_GENERATOR "c" CACHE STRING "" FORCE)
set(GLAD_EXTENSIONS "" CACHE STRING "" FORCE)
# GLAD v0.1.36 declares cmake_minimum_required(VERSION 2.8) which triggers a
# CMake deprecation warning. This is GLAD's code, not ours — suppress it for
# this subdirectory only, then restore normal warning behaviour.
set(CMAKE_WARN_DEPRECATED FALSE CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glad)
set(CMAKE_WARN_DEPRECATED TRUE CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# SDL2
# ---------------------------------------------------------------------------
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(SDL_SHARED  OFF CACHE BOOL "" FORCE)
set(SDL_STATIC  ON  CACHE BOOL "" FORCE)
# SDL2 2.30.9 declares an old cmake_minimum_required — same pattern as GLAD.
set(CMAKE_WARN_DEPRECATED FALSE CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)
set(CMAKE_WARN_DEPRECATED TRUE CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# entt  (header-only ECS)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.14.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(entt)

# ---------------------------------------------------------------------------
# glm  (header-only 3D math: vec/mat/quat, matrix builders for view/proj/etc)
# Used by 3D rendering paths (selva-oscura). Provides the same vector/matrix
# semantics GLSL uses, so CPU-side math composes cleanly with shader code.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(glm)

# ---------------------------------------------------------------------------
# nlohmann/json  (header-only)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# Catch2  (unit testing — C++ equivalent of Jest)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Catch2)

# ---------------------------------------------------------------------------
# Tracy  (real-time profiler — disabled by default, enable with -DTRACY_ENABLE=ON)
# When disabled all Tracy macros (FrameMark, ZoneScoped) compile to nothing.
# To profile: build with -DTRACY_ENABLE=ON and connect the Tracy server app.
# ---------------------------------------------------------------------------
option(TRACY_ENABLE "Enable Tracy profiler client" OFF)
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        v0.11.1
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(tracy)

# ---------------------------------------------------------------------------
# miniaudio  (header-only audio — single file, zero dependencies)
# Handles SFX mixing, music looping, and device output.
# MINIAUDIO_IMPLEMENTATION must be defined in exactly one .cpp file (AudioSystem.cpp).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(miniaudio)

add_library(miniaudio_iface INTERFACE)
add_library(miniaudio::miniaudio ALIAS miniaudio_iface)
target_include_directories(miniaudio_iface SYSTEM INTERFACE
    ${miniaudio_SOURCE_DIR}
    ${miniaudio_SOURCE_DIR}/extras)

# ---------------------------------------------------------------------------
# FMOD  (stub — replace with real SDK integration when ready)
# Wire up: point FMOD_ROOT at the extracted FMOD SDK directory, then swap
# this stub out for real include/link targets in a future issue.
# ---------------------------------------------------------------------------
add_library(fmod_stub INTERFACE)
add_library(FMOD::Core ALIAS fmod_stub)

# ---------------------------------------------------------------------------
# Dear ImGui  (immediate-mode UI for in-game tuning panels and dev overlays)
# Source-only release; we build a small static lib here against the SDL2 +
# OpenGL3 backends. Used by selva-oscura's tuning UI; available to any game.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(imgui)

# ImGui doesn't ship its own CMake target — assemble one ourselves from the
# core sources + the two backend files we need (SDL2 platform + OpenGL3
# renderer). Marked SYSTEM so clang-tidy doesn't fire on ImGui's internals.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL2::SDL2-static OpenGL::GL)
add_library(imgui::imgui ALIAS imgui)

# ---------------------------------------------------------------------------
# cgltf  (single-header C library for parsing glTF 2.0 files, .gltf and .glb)
# Used by selva-oscura to load skinned characters + animation clips. We
# include the header directly; CGLTF_IMPLEMENTATION must be defined in
# exactly one .cpp file (we'll do that in selva-oscura's asset loader).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(cgltf)

add_library(cgltf_iface INTERFACE)
target_include_directories(cgltf_iface SYSTEM INTERFACE ${cgltf_SOURCE_DIR})
add_library(cgltf::cgltf ALIAS cgltf_iface)

# ---------------------------------------------------------------------------
# ozz-animation  (C++ skeletal-animation runtime — clip sampling, blending,
# bone palette computation). Two pieces:
#   * runtime  : samples clips, computes bone palettes (links into the game)
#   * offline  : converts glTF/Collada → ozz's binary format (build-time tool)
# Used by selva-oscura's skeletal driver.
# ---------------------------------------------------------------------------
set(ozz_build_samples       OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos         OFF CACHE BOOL "" FORCE)
set(ozz_build_tests          OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx            OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf            ON CACHE BOOL "" FORCE)
set(ozz_build_tools           ON CACHE BOOL "" FORCE)
set(ozz_build_data           OFF CACHE BOOL "" FORCE)
set(ozz_build_postfix        OFF CACHE BOOL "" FORCE)
set(ozz_build_msvc_rt_dll    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ozz
    GIT_REPOSITORY https://github.com/guillaumeblanc/ozz-animation.git
    GIT_TAG        0.16.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
# ozz uses an older cmake_minimum — same workaround pattern as GLAD/SDL2.
set(CMAKE_WARN_DEPRECATED FALSE CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ozz)
set(CMAKE_WARN_DEPRECATED TRUE CACHE BOOL "" FORCE)
