add_executable(prison-break-game
    src/main.cpp
)

target_link_libraries(prison-break-game PRIVATE engine)

# Sync assets and config to the build output on every build.
#
# Using add_custom_target with ALL instead of POST_BUILD on the game target:
# POST_BUILD only fires when the exe actually relinks. If only a JSON or PNG
# changes, CMake sees no source changes, skips the build, and POST_BUILD never
# runs — so the stale build-output copy keeps loading instead of the edited
# source file.  An ALL custom target runs unconditionally on every F7 build,
# so config/asset edits are always reflected without touching any .cpp file.
add_custom_target(sync-assets-and-config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:prison-break-game>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:prison-break-game>/config
    COMMENT "Syncing assets and config to build output..."
    VERBATIM
)
