# Selva Oscura — 3D soulslike running on the main engine. Visual identity:
# pure 3D meshes with 1-bit shader treatment (locked 2026-05-05); see
# engines/engine/docs/3D-EXTENSION.md for the architectural plan.

# Per-game project declaration -- the release pipeline reads this version
# when a PR with prefix <bump>/selva-oscura/... merges. Starts at 0.0.0;
# the first <bump>/selva-oscura/... merge advances it from there.
project(SelvaOscura VERSION 0.0.0 LANGUAGES C CXX)

# Game-side library — animation drivers, future combat / movement systems.
# Shared between the game executable and the test target. Keep it static;
# selva-oscura is one exe today.
add_library(selva-oscura-lib STATIC
    src/anim/ProceduralDriver.cpp
)

target_include_directories(selva-oscura-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(selva-oscura-lib PUBLIC engine)

# Game executable.
add_executable(selva-oscura src/main.cpp)
target_link_libraries(selva-oscura PRIVATE selva-oscura-lib)

# Static-link the MinGW runtime so the exe runs without libgcc/libstdc++ DLLs.
# Mirrors the same option on prison-break-game; needed for distributable builds.
if(MINGW)
    target_link_options(selva-oscura PRIVATE -static-libgcc -static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# Game tests — pure-compute logic that doesn't need a GL context (animation
# drivers, dodge state machine math). Systems that need SDL/GL are tested
# by running the game itself.
# ---------------------------------------------------------------------------
add_executable(selva-oscura-tests
    tests/animation_driver_test.cpp
)
target_link_libraries(selva-oscura-tests PRIVATE selva-oscura-lib Catch2::Catch2WithMain)

add_test(NAME selva-oscura-tests COMMAND selva-oscura-tests)
