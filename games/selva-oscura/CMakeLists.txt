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
    src/anim/AnimationClip.cpp
    src/anim/PoseSampler.cpp
    src/anim/ProceduralDriver.cpp
    src/anim/Skeleton.cpp
    src/anim/SkeletalMesh.cpp
    src/anim/SkeletalRenderer.cpp
    src/Tunables.cpp
)

target_include_directories(selva-oscura-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(selva-oscura-lib
    PUBLIC
        engine
        ozz_animation
        ozz_geometry
        ozz_base
        cgltf::cgltf
)

# Game executable.
add_executable(selva-oscura src/main.cpp)
target_link_libraries(selva-oscura PRIVATE selva-oscura-lib)

# Static-link the MinGW runtime so the exe runs without libgcc/libstdc++ DLLs.
# Mirrors the same option on prison-break-game; needed for distributable builds.
if(MINGW)
    target_link_options(selva-oscura PRIVATE -static-libgcc -static-libstdc++)
endif()

# Sync config + assets to the build output on every build so the exe finds
# config/tunables.json and assets/characters/.../Soldier.glb at runtime.
# ALL custom target (not POST_BUILD) so file edits are picked up even when
# no .cpp changed; same pattern as prison-escape-game.
add_custom_target(selva-oscura-sync-config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:selva-oscura>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:selva-oscura>/assets
    COMMENT "Syncing selva-oscura config + assets to build output..."
    VERBATIM
)

# ---------------------------------------------------------------------------
# Convert Soldier.glb → skeleton.ozz + Idle.ozz + Walk.ozz + Run.ozz + TPose.ozz
# at build time, using the gltf2ozz tool from the vendored ozz-animation.
# Runs in the output directory so the tool's relative-path file writes land
# next to the .glb. The .ozz files are NOT committed (see assets/.gitignore);
# they're derived data regenerated whenever Soldier.glb or gltf2ozz changes.
# ---------------------------------------------------------------------------
set(soldier_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/characters/soldier)
# Use CMAKE_RUNTIME_OUTPUT_DIRECTORY (set in root CMakeLists to build/bin) —
# add_custom_command's OUTPUT can't contain generator expressions, so the
# $<TARGET_FILE_DIR:...> form would fail at generate time.
set(soldier_out_dir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/characters/soldier)
set(soldier_ozz_files
    ${soldier_out_dir}/skeleton.ozz
    ${soldier_out_dir}/Idle.ozz
    ${soldier_out_dir}/Walk.ozz
    ${soldier_out_dir}/Run.ozz
    ${soldier_out_dir}/TPose.ozz
)
add_custom_command(
    OUTPUT ${soldier_ozz_files}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${soldier_out_dir}
    COMMAND ${CMAKE_COMMAND} -E chdir ${soldier_out_dir}
        $<TARGET_FILE:gltf2ozz>
        --file=${soldier_dir}/Soldier.glb
    DEPENDS ${soldier_dir}/Soldier.glb gltf2ozz
    COMMENT "Converting Soldier.glb to ozz runtime format..."
    VERBATIM
)
add_custom_target(selva-oscura-convert-soldier ALL DEPENDS ${soldier_ozz_files})

# ---------------------------------------------------------------------------
# Game tests — pure-compute logic that doesn't need a GL context (animation
# drivers, dodge state machine math). Systems that need SDL/GL are tested
# by running the game itself.
# ---------------------------------------------------------------------------
add_executable(selva-oscura-tests
    tests/animation_driver_test.cpp
    tests/skeletal_loader_test.cpp
)
target_link_libraries(selva-oscura-tests PRIVATE selva-oscura-lib Catch2::Catch2WithMain)

# Run the test exe from build/bin so the relative paths in
# skeletal_loader_test.cpp ("assets/characters/...") resolve to the .ozz
# files produced by the gltf2ozz custom command. Also make the test depend
# on the conversion target so the .ozz files exist by the time it runs.
add_test(
    NAME selva-oscura-tests
    COMMAND selva-oscura-tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
add_dependencies(selva-oscura-tests selva-oscura-convert-soldier)
