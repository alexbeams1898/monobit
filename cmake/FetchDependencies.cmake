include(FetchContent)

# ---------------------------------------------------------------------------
# OpenGL  (system library — not fetched, just located)
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

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
# FMOD  (stub — replace with real SDK integration when ready)
# Wire up: point FMOD_ROOT at the extracted FMOD SDK directory, then swap
# this stub out for real include/link targets in a future issue.
# ---------------------------------------------------------------------------
add_library(fmod_stub INTERFACE)
add_library(FMOD::Core ALIAS fmod_stub)
