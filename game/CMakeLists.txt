# Game-specific logic -- static library linked by both the game exe and tests.
add_library(game-systems STATIC
    src/GameLoop.cpp
    src/systems/WaveSystem.cpp
    src/systems/CombatSystem.cpp
    src/systems/DamageSystem.cpp
    src/systems/DeathSystem.cpp
    src/systems/LevelingSystem.cpp
    src/systems/PickupSystem.cpp
    src/systems/RestSpotSystem.cpp
    src/systems/AggroSystem.cpp
    src/systems/ChaseSystem.cpp
    src/systems/SteeringSystem.cpp
    src/systems/MovementSystem.cpp
    src/systems/ParticleSystem.cpp
    src/systems/SpawnerSystem.cpp
)

target_include_directories(game-systems PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(game-systems PUBLIC engine)

# Game executable.
add_executable(prison-break-game src/main.cpp)
target_link_libraries(prison-break-game PRIVATE game-systems)

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
)
target_link_libraries(game-tests PRIVATE game-systems Catch2::Catch2WithMain)

get_target_property(_CATCH2_INCLUDES Catch2::Catch2 INTERFACE_INCLUDE_DIRECTORIES)
if(_CATCH2_INCLUDES)
    target_include_directories(game-tests SYSTEM PRIVATE ${_CATCH2_INCLUDES})
endif()

add_test(NAME game-tests COMMAND game-tests)
set_tests_properties(game-tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/game)
