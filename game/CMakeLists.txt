# Game-specific logic -- static library linked by both the game exe and tests.
add_library(game-systems STATIC
    src/GameLoop.cpp
    src/ConfigLoader.cpp
    src/systems/InputMappingSystem.cpp
    src/systems/AnimStateSystem.cpp
    src/systems/WaveSystem.cpp
    src/systems/CombatSystem.cpp
    src/systems/DamageSystem.cpp
    src/systems/DeathSystem.cpp
    src/systems/LevelingSystem.cpp
    src/systems/PickupSystem.cpp
    src/systems/RestSpotSystem.cpp
    src/systems/LadderSystem.cpp
    src/systems/AggroSystem.cpp
    src/systems/AmbientSoundSystem.cpp
    src/systems/ChaseSystem.cpp
    src/systems/MovementSystem.cpp
    src/systems/ParticleSystem.cpp
    src/systems/SpawnerSystem.cpp
    src/systems/TintSystem.cpp
    src/ops/SpawnUtils.cpp
    src/ops/InventoryOps.cpp
    src/ops/CraftingOps.cpp
    src/systems/EquipmentSystem.cpp
    src/systems/WeaponXPSystem.cpp
    src/systems/CraftingSystem.cpp
    src/systems/NotificationSystem.cpp
    src/renderers/HudRenderer.cpp
    src/renderers/InteractionPromptRenderer.cpp
    src/renderers/ItemStatRenderer.cpp
    src/screens/PauseMenu.cpp
    src/screens/InventoryScreen.cpp
    src/screens/ConfirmDialog.cpp
    src/screens/CraftingScreen.cpp
    src/screens/MenuDialog.cpp
    src/screens/LevelUpScreen.cpp
    src/SaveManager.cpp
    src/WorldInit.cpp
    src/renderers/AIDebugOverlay.cpp
    src/renderers/AIRecorder.cpp
    src/renderers/DebugOverlay.cpp
    src/screens/MainMenuScreen.cpp
    src/screens/CharCreateScreen.cpp
    src/screens/LoadGameScreen.cpp
    src/screens/GameOverScreen.cpp
    src/screens/VictoryScreen.cpp
    src/screens/RunSummaryScreen.cpp
    src/screens/HighScoresScreen.cpp
    src/screens/SanctuaryScreen.cpp
)

target_include_directories(game-systems PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(game-systems PUBLIC engine)

# Game executable.
add_executable(prison-break-game src/main.cpp)
target_link_libraries(prison-break-game PRIVATE game-systems)

# Static-link MinGW runtime so the exe runs without libgcc/libstdc++ DLLs.
if(MINGW)
    target_link_options(prison-break-game PRIVATE -static-libgcc -static-libstdc++)
endif()

# Sync assets and config to the build output on every build.
#
# Using add_custom_target with ALL instead of POST_BUILD on the game target:
# POST_BUILD only fires when the exe actually relinks. If only a JSON or PNG
# changes, CMake sees no source changes, skips the build, and POST_BUILD never
# runs -- so the stale build-output copy keeps loading instead of the edited
# source file.  An ALL custom target runs unconditionally on every F7 build,
# so config/asset edits are always reflected without touching any .cpp file.
add_custom_target(sync-assets-and-config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/game/assets
        $<TARGET_FILE_DIR:prison-break-game>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/game/config
        $<TARGET_FILE_DIR:prison-break-game>/config
    COMMENT "Syncing assets and config to build output..."
    VERBATIM
)

# ---------------------------------------------------------------------------
# Game tests -- test game systems (links game-systems which PUBLIC-links engine).
# ---------------------------------------------------------------------------
add_executable(game-tests
    tests/camera_test.cpp
    tests/systems_test.cpp
    tests/config_test.cpp
    tests/movement_system_test.cpp
    tests/steering_system_test.cpp
    tests/chase_system_test.cpp
    tests/aggro_system_test.cpp
    tests/spawner_system_test.cpp
    tests/combat_test.cpp
    tests/leveling_test.cpp
    tests/particle_system_test.cpp
    tests/wave_system_test.cpp
    tests/inventory_test.cpp
    tests/equipment_system_test.cpp
    tests/drop_system_test.cpp
    tests/crafting_test.cpp
    tests/animation_test.cpp
    tests/weapon_xp_test.cpp
    tests/evolution_test.cpp
    tests/armor_defense_test.cpp
    tests/discovery_test.cpp
    tests/ladder_test.cpp
)
target_link_libraries(game-tests PRIVATE game-systems Catch2::Catch2WithMain)

add_test(NAME game-tests COMMAND game-tests)
set_tests_properties(game-tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/game)
