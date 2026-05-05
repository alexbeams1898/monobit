# Selva Oscura — currently a window-only smoke test of the engine.
# This file will grow as the game grows; for now it's the smallest thing
# that produces a runnable executable linking the engine library.

# Per-game project declaration -- the release pipeline reads this version
# when a PR with prefix <bump>/selva-oscura/... merges. Starts at 0.0.0;
# the first <bump>/selva-oscura/... merge advances it from there.
project(SelvaOscura VERSION 0.0.0 LANGUAGES C CXX)

add_executable(selva-oscura src/main.cpp)

target_link_libraries(selva-oscura PRIVATE engine)

# Static-link the MinGW runtime so the exe runs without libgcc/libstdc++ DLLs.
# Mirrors the same option on prison-break-game; needed for distributable builds.
if(MINGW)
    target_link_options(selva-oscura PRIVATE -static-libgcc -static-libstdc++)
endif()
