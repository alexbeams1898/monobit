# Per-game project declaration -- gives this game its own VERSION that the
# release pipeline reads when a PR with prefix <bump>/point-of-entry/... merges.
project(PointOfEntry VERSION 0.1.0 LANGUAGES C CXX)

# Game-specific logic -- static library linked by both the game exe and tests.
# Kept separate from the exe so tests can link the same code without main().
add_library(poe-systems STATIC
    src/ops/CaptureUtils.cpp
    src/renderers/DebugPanelRenderer.cpp
    src/screens/PauseScreen.cpp
    src/screens/StagingScreen.cpp
    src/screens/ScreenStyle.cpp
    src/screens/ScreenInput.cpp
    src/systems/ChaseSystem.cpp
    src/renderers/FloaterRenderer.cpp
    src/systems/RewardSystem.cpp
    src/systems/ThermosSystem.cpp
    src/ecs/ItemConfig.cpp
    src/ops/LootOps.cpp
    src/systems/PickupSystem.cpp
    src/ecs/BalanceConfig.cpp
    src/systems/WaveSystem.cpp
    src/systems/AimSystem.cpp
    src/renderers/HudRenderer.cpp
    src/renderers/NotificationRenderer.cpp
    src/renderers/PromptRenderer.cpp
    src/systems/CombatSystem.cpp
    src/ops/NavUtils.cpp
    src/ops/RecordOps.cpp
    src/ops/GuideOps.cpp
    src/systems/GaitSystem.cpp
    src/systems/DamageSystem.cpp
    src/systems/SpriteAnimSystem.cpp
    src/SpriteDefLoader.cpp
    src/screens/TitleScreen.cpp
    src/FloorGen.cpp
    src/AreaLoader.cpp
    src/ops/SpawnUtils.cpp
    src/ops/ZoneUtils.cpp
    src/ops/AreaBuildOps.cpp
    src/systems/TravelSystem.cpp
    src/systems/DescentSystem.cpp
    src/systems/PlayerSystem.cpp
)

target_include_directories(poe-systems PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(poe-systems PUBLIC engine)
# stb_image_write backs the F12 frame capture (see include/Capture.h).
target_include_directories(poe-systems SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor/stb)

# Game executable. Each game's exe + assets live in their own subdirectory under
# build/bin/ so two games' config/ trees can never collide.
add_executable(point-of-entry src/main.cpp)
target_link_libraries(point-of-entry PRIVATE poe-systems)
set_target_properties(point-of-entry PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/point-of-entry)

# Static-link the MinGW runtime so the exe runs without libgcc/libstdc++ DLLs.
if(MINGW)
    target_link_options(point-of-entry PRIVATE -static-libgcc -static-libstdc++)
endif()

# Dev builds read assets + config straight from the SOURCE tree (the exe chdirs
# there at boot): one source of truth, so a config or map edit -- including maps
# the in-game editor writes back -- is live on the next launch with no copy step
# to lag or overwrite it. Release builds ship the copy beside the exe instead.
option(POE_RELEASE "Package assets beside the exe instead of reading the source tree" OFF)
if(POE_RELEASE)
    add_custom_target(poe-sync-assets-and-config ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/config
            $<TARGET_FILE_DIR:point-of-entry>/config
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:point-of-entry>/assets
        COMMENT "point-of-entry: syncing config/ + assets/"
    )
    add_dependencies(point-of-entry poe-sync-assets-and-config)
else()
    target_compile_definitions(point-of-entry PRIVATE
        POE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Tests -- link the same poe-systems the game does, so what is tested is what ships.
add_executable(poe-tests
    tests/floorgen_test.cpp
    tests/sprite_anim_test.cpp
    tests/loot_test.cpp
    tests/reward_test.cpp
    tests/stats_test.cpp
    tests/area_test.cpp
    tests/descent_test.cpp
    tests/record_test.cpp
    tests/guide_test.cpp
    tests/screen_test.cpp
)
target_link_libraries(poe-tests PRIVATE poe-systems Catch2::Catch2WithMain)
add_test(NAME poe-tests COMMAND poe-tests)
# Relative paths like "config/floor.json" resolve against the project root.
set_tests_properties(poe-tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
