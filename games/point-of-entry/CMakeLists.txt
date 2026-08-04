# Per-game project declaration -- gives this game its own VERSION that the
# release pipeline reads when a PR with prefix <bump>/point-of-entry/... merges.
project(PointOfEntry VERSION 0.1.0 LANGUAGES C CXX)

# Game-specific logic -- static library linked by both the game exe and tests.
# Kept separate from the exe so tests can link the same code without main().
add_library(poe-systems STATIC
    src/Capture.cpp
    src/FloorGen.cpp
    src/Player.cpp
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

# Assets + config live beside the exe. Copied on every build so an edit to a
# .room file or a config value is picked up by the next run without a reconfigure.
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
