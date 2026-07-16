# Per-game project declaration -- gives this game its own VERSION that the
# release pipeline reads when a PR with prefix <bump>/wayworn-hush/... merges.
project(WaywornHush VERSION 0.1.0 LANGUAGES C CXX)

# Game-specific logic -- static library linked by both the game exe and tests.
add_library(wayworn-systems STATIC
    src/GameLoop.cpp
    src/WorldInit.cpp
    src/PlayerMovement.cpp
    src/PlayerConfig.cpp
    src/Observations.cpp
    src/UnlockCondition.cpp
    src/Growth.cpp
    src/ThoughtBox.cpp
    src/Glimmer.cpp
    src/Notify.cpp
    src/Footsteps.cpp
    src/Surfaces.cpp
    src/Structures.cpp
    src/JsonConfig.cpp
    src/WorldConfig.cpp
    src/Interaction.cpp
    src/HudCanvas.cpp
    src/HeadMarker.cpp
    src/WorldClock.cpp
    src/Inventory.cpp
    src/Loot.cpp
    src/Formulas.cpp
    src/Crafting.cpp
    src/WorldItems.cpp
    src/InteractionMode.cpp
    src/Notebook.cpp
    src/LdtkImport.cpp
    src/PausePage.cpp
    src/SaveGame.cpp
    src/ScreenStyle.cpp
    src/LoadScreen.cpp
    src/NameScreen.cpp
    src/TextField.cpp
    src/TitleScreen.cpp
    src/TunePanel.cpp
)

target_include_directories(wayworn-systems PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# vendored stb (the engine defines STB_IMAGE_IMPLEMENTATION once in TextureManager);
# the LDtk importer includes stb_image.h for declarations to read atlas alpha.
target_include_directories(wayworn-systems SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor/stb)
target_link_libraries(wayworn-systems PUBLIC engine)

# Generate Version.h from CMake project version.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h
    @ONLY
)
target_include_directories(wayworn-systems PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Game executable.
add_executable(wayworn-hush src/main.cpp)
target_link_libraries(wayworn-hush PRIVATE wayworn-systems)
set_target_properties(wayworn-hush PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/wayworn-hush)

# Static-link MinGW runtime so the exe runs without libgcc/libstdc++ DLLs.
if(MINGW)
    target_link_options(wayworn-hush PRIVATE -static-libgcc -static-libstdc++)
endif()

# A shareable build: the exe reads assets from beside itself rather than chdir'ing
# to THIS machine's source tree, so it runs anywhere. Off by default -- F7 wants the
# dev behaviour. scripts/package.sh turns it on for the copy it zips.
option(WAYWORN_RELEASE "Build an exe that reads assets beside it, for sharing" OFF)

# Dev builds read assets/config DIRECTLY from the source tree (main.cpp chdir's
# here at startup): one source of truth, so an LDtk/JSON/PNG edit is live on the
# next F7 -- no copy step that can lag or lock behind a running game. Release
# builds (WAYWORN_RELEASE=ON) omit this and ship assets beside the exe.
if(NOT WAYWORN_RELEASE)
    target_compile_definitions(wayworn-hush PRIVATE
        WAYWORN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# ---------------------------------------------------------------------------
# Game tests -- test game systems (links wayworn-systems which PUBLIC-links engine).
# ---------------------------------------------------------------------------
add_executable(wayworn-tests
    tests/scaffold_test.cpp
    tests/observations_test.cpp
    tests/growth_test.cpp
    tests/pause_page_test.cpp
    tests/unlock_condition_test.cpp
    tests/footsteps_test.cpp
    tests/surfaces_test.cpp
    tests/structures_test.cpp
    tests/screen_to_world_test.cpp
    tests/interaction_test.cpp
    tests/hud_canvas_test.cpp
    tests/head_marker_test.cpp
    tests/world_clock_test.cpp
    tests/inventory_test.cpp
    tests/loot_test.cpp
    tests/formulas_test.cpp
    tests/savegame_test.cpp
    tests/player_movement_test.cpp
    tests/title_screen_test.cpp
    tests/text_field_test.cpp
    tests/world_items_test.cpp
    tests/crafting_test.cpp
    tests/notebook_test.cpp
    tests/ldtk_import_test.cpp
)
target_link_libraries(wayworn-tests PRIVATE wayworn-systems Catch2::Catch2WithMain)
set_target_properties(wayworn-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/wayworn-hush)

add_test(NAME wayworn-tests COMMAND wayworn-tests)
set_tests_properties(wayworn-tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
