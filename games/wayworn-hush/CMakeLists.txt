# Per-game project declaration -- gives this game its own VERSION that the
# release pipeline reads when a PR with prefix <bump>/wayworn-hush/... merges.
project(WaywornHush VERSION 0.1.0 LANGUAGES C CXX)

# Game-specific logic -- static library linked by both the game exe and tests.
add_library(wayworn-systems STATIC
    src/GameLoop.cpp
    src/WorldInit.cpp
    src/PlayerMovement.cpp
)

target_include_directories(wayworn-systems PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(wayworn-systems PUBLIC engine)

# Generate Version.h from CMake project version.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h
    @ONLY
)
target_include_directories(wayworn-systems PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Game executable. Each game's exe + assets live in their own subdirectory
# under build/bin/ so two games' config/ trees can never collide.
add_executable(wayworn-hush src/main.cpp)
target_link_libraries(wayworn-hush PRIVATE wayworn-systems)
set_target_properties(wayworn-hush PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/wayworn-hush)

# Static-link MinGW runtime so the exe runs without libgcc/libstdc++ DLLs.
if(MINGW)
    target_link_options(wayworn-hush PRIVATE -static-libgcc -static-libstdc++)
endif()

# Sync assets and config to the build output on every build (ALL target, not
# POST_BUILD, so JSON/PNG edits sync even when no .cpp changes -- see
# prison-escape-game/CMakeLists.txt for the full rationale). The dirs are
# empty at scaffold stage; the target is harmless and ready for content.
add_custom_target(wayworn-sync-assets-and-config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:wayworn-hush>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:wayworn-hush>/config
    COMMENT "Syncing wayworn-hush assets and config to build output..."
    VERBATIM
)

# ---------------------------------------------------------------------------
# Game tests -- test game systems (links wayworn-systems which PUBLIC-links engine).
# ---------------------------------------------------------------------------
add_executable(wayworn-tests
    tests/scaffold_test.cpp
)
target_link_libraries(wayworn-tests PRIVATE wayworn-systems Catch2::Catch2WithMain)
set_target_properties(wayworn-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/wayworn-hush)

add_test(NAME wayworn-tests COMMAND wayworn-tests)
set_tests_properties(wayworn-tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
