add_executable(prison-break-game
    src/main.cpp
)

target_link_libraries(prison-break-game PRIVATE engine)

# Copy assets and config to the build output directory after each build
add_custom_command(TARGET prison-break-game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:prison-break-game>/assets
    COMMENT "Copying assets..."
)

add_custom_command(TARGET prison-break-game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:prison-break-game>/config
    COMMENT "Copying config..."
)
