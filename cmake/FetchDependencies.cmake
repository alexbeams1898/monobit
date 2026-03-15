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
)
set(GLAD_PROFILE   "core" CACHE STRING "" FORCE)
set(GLAD_API       "gl=3.3" CACHE STRING "" FORCE)
set(GLAD_GENERATOR "c" CACHE STRING "" FORCE)
set(GLAD_EXTENSIONS "" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(glad)

# ---------------------------------------------------------------------------
# SDL2
# ---------------------------------------------------------------------------
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED  OFF CACHE BOOL "" FORCE)
set(SDL_STATIC  ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# ---------------------------------------------------------------------------
# entt  (header-only ECS)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(entt)

# ---------------------------------------------------------------------------
# nlohmann/json  (header-only)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
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
)
FetchContent_MakeAvailable(tracy)

# ---------------------------------------------------------------------------
# FMOD  (stub — replace with real SDK integration when ready)
# Wire up: point FMOD_ROOT at the extracted FMOD SDK directory, then swap
# this stub out for real include/link targets in a future issue.
# ---------------------------------------------------------------------------
add_library(fmod_stub INTERFACE)
add_library(FMOD::Core ALIAS fmod_stub)
