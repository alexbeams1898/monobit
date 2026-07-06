# Selva Oscura — 3D soulslike running on the main engine. Visual identity:
# pure 3D meshes with 1-bit shader treatment (locked 2026-05-05); see
# engines/engine/docs/3D-EXTENSION.md for the architectural plan.

# Per-game project declaration -- the release pipeline reads this version
# when a PR with prefix <bump>/selva-oscura/... merges. Starts at 0.0.0;
# the first <bump>/selva-oscura/... merge advances it from there.
project(SelvaOscura VERSION 0.3.0 LANGUAGES C CXX)

# Build fingerprint -- short git commit hash + configure timestamp.
# Stamped into the binary as compile defines so the boot log can
# print them unconditionally. Lets us answer "which exact binary
# did the player run?" when investigating crashes / bug reports.
# Re-evaluated every CMake configure -- not every build -- which is
# fine because the binary's own mtime is the second half of the
# fingerprint.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE SELVA_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT SELVA_GIT_COMMIT)
    set(SELVA_GIT_COMMIT "unknown")
endif()
string(TIMESTAMP SELVA_CONFIGURE_TIME "%Y-%m-%dT%H:%M:%S")

# Python is used by the FBX → glb → ozz bake to glob the gltf2ozz
# output regardless of its internal animation name. Required on MSYS2
# already (GLAD uses it at configure time); this just exposes the
# interpreter path for our custom commands.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Game-side library — animation drivers, future combat / movement systems.
# Shared between the game executable and the test target. Keep it static;
# selva-oscura is one exe today.
add_library(selva-oscura-lib STATIC
    src/anim/AnimationClip.cpp
    src/anim/AnimIntent.cpp
    src/anim/AnimSet.cpp
    src/anim/AnimSetRegistry.cpp
    src/anim/ClipGeometry.cpp
    src/anim/ClipRegistry.cpp
    src/audio/Audio.cpp
    src/anim/LocomotionConfig.cpp
    src/anim/PoseSampler.cpp
    src/anim/SkeletalAssets.cpp
    src/anim/Skeleton.cpp
    src/anim/SkeletalMesh.cpp
    src/anim/SkeletalRenderer.cpp
    src/anim/SkeletonJointMap.cpp
    src/gameplay/Actor.cpp
    src/gameplay/AiTick.cpp
    src/gameplay/BehaviorTree.cpp
    src/gameplay/BossDispatcher.cpp
    src/gameplay/BossRewards.cpp
    src/gameplay/BossState.cpp
    src/gameplay/Enemies.cpp
    src/gameplay/EnemyArchetype.cpp
    src/gameplay/Faction.cpp
    src/gameplay/Footsteps.cpp
    src/gameplay/LocomotionStateMachine.cpp
    src/gameplay/PerFrameTick.cpp
    src/gameplay/Perception.cpp
    src/gameplay/PlayerState.cpp
    src/gameplay/PropArchetype.cpp
    src/gameplay/PropSpawn.cpp
    src/gameplay/RomanNumeral.cpp
    src/gameplay/RomanTiers.cpp
    src/gameplay/Appearance.cpp
    src/gameplay/AuthoredCharacter.cpp
    src/gameplay/AppearanceDeformation.cpp
    src/gameplay/AppearanceRegistry.cpp
    src/gameplay/AppearanceTransformWriter.cpp
    src/gameplay/AppearanceTransformBootstrap.cpp
    src/gameplay/AppearanceVariance.cpp
    src/gameplay/Sangue.cpp
    src/gameplay/SanguePulse.cpp
    src/gameplay/ScriptedEvents.cpp
    src/gameplay/TerritoryClamp.cpp
    src/combat/ActorVolumes.cpp
    src/combat/AttackChain.cpp
    src/combat/AttackResolution.cpp
    src/combat/ChainObserver.cpp
    src/combat/CombatData.cpp
    src/combat/CombatLog.cpp
    src/combat/HandCycle.cpp
    src/combat/QuickSlot.cpp
    src/combat/HitDetection.cpp
    src/combat/HitFeedback.cpp
    src/combat/HitVolumes.cpp
    src/combat/PressMapping.cpp
    src/combat/SpliceDiag.cpp
    src/combat/TransitionProfile.cpp
    src/combat/WeaponData.cpp
    src/insight/Insight.cpp
    src/insight/InsightLayout.cpp
    src/lang/Language.cpp
    src/render/Atmosphere.cpp
    src/render/Camera.cpp
    src/render/RegionGeometry.cpp
    src/render/RegionShaders.cpp
    src/render/ShadowPass.cpp
    src/render/LightSpritePass.cpp
    src/render/PickupSpritePass.cpp
    src/render/PickupMeshPass.cpp
    src/render/EquippedWeapon.cpp
    src/render/HairRenderer.cpp
    src/hair/HairRegistry.cpp
    src/render/SkyPass.cpp
    src/render/TerrainShader.cpp
    src/render/Texture.cpp
    src/render/TreeShader.cpp
    src/render/WorldRenderer.cpp
    src/world/Door.cpp
    src/world/JsonRegion.cpp
    src/world/PhysicsRegion.cpp
    src/world/RegionBootstrap.cpp
    src/world/StaticMeshAssets.cpp
    src/world/Terrain.cpp
    src/world/TreeAssets.cpp
    src/ui/ActorHud.cpp
    src/ui/BossHud.cpp
    src/ui/AppearanceEditor.cpp
    src/ui/CharacterCreationScreen.cpp
    src/ui/CharacterPreview.cpp
    src/ui/ClassPickerScreen.cpp
    src/ui/ComboHud.cpp
    src/ui/DialogScreen.cpp
    src/ui/InteractionPrompt.cpp
    src/ui/Notifications.cpp
    src/ui/Screens.cpp
    src/ui/TuningPanel.cpp
    src/ui/UIComponents.cpp
    src/world/Collision.cpp
    src/AppStateGlobal.cpp
    src/PlayerClass.cpp
    src/SaveManager.cpp
    src/dialog/DialogSystem.cpp
    src/dialog/GuideHandlers.cpp
    src/dialog/Handlers.cpp
    src/dialog/TopicRegistry.cpp
    src/interact/Interaction.cpp
    src/classmods/ClassModifiers.cpp
    src/items/CategoryRegistry.cpp
    src/debug/Flags.cpp
    src/hazard/HazardZones.cpp
    src/identity/Identity.cpp
    src/items/ItemRegistry.cpp
    src/items/UseHandlers.cpp
    src/items/HealHandlers.cpp
    src/loot/Pickups.cpp
    src/loot/StarterDispenser.cpp
    src/notice/Notices.cpp
    src/ops/InventoryOps.cpp
    src/softcaps/SoftCaps.cpp
    src/spawn/FlowSpawner.cpp
    src/gather/GatherSpawner.cpp
    src/text/Examine.cpp
    src/text/TextPresentation.cpp
    src/Tunables.cpp
    src/Formulas.cpp
    src/Scene.cpp
    src/WallClock.cpp
)

target_include_directories(selva-oscura-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Vendored single-header stb_image_write for the F1-debug frame capture.
# Marked SYSTEM so clang-tidy stays out of stb's internals.
target_include_directories(selva-oscura-lib SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor/stb)
target_link_libraries(selva-oscura-lib
    PUBLIC
        engine
        ozz_animation
        ozz_geometry
        ozz_base
        cgltf::cgltf
)

# Game executable. Per-game output subdirectory under build/bin/ so config
# trees can never collide with other games' (Selva and PE both have a
# config/balance/formulas.json -- without per-game dirs the second-to-sync
# clobbers the first at the shared build/bin/ path).
add_executable(selva-oscura WIN32 src/main.cpp)
target_link_libraries(selva-oscura PRIVATE selva-oscura-lib)
target_include_directories(selva-oscura SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor/stb)
# Build fingerprint -- visible in the boot log + crash dumps so we
# can always answer "which exact binary did this come from?"
target_compile_definitions(selva-oscura PRIVATE
    SELVA_GIT_COMMIT="${SELVA_GIT_COMMIT}"
    SELVA_CONFIGURE_TIME="${SELVA_CONFIGURE_TIME}")
set_target_properties(selva-oscura PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/selva-oscura)

# Static-link the MinGW runtime so the exe runs without libgcc/libstdc++ DLLs.
# Mirrors the same option on prison-break-game; needed for distributable builds.
if(MINGW)
    target_link_options(selva-oscura PRIVATE -static-libgcc -static-libstdc++)
endif()

# Crash-dump symbol resolution -- MinGW GCC's default -g emits DWARF
# which Windows DbgHelp cannot read; the engine::CrashHandler dump
# would then show only addresses (frame #10 0x00007ff... rather than
# selva::gameplay::tickEnemies (Enemies.cpp:1234)). -gcodeview emits
# MS CodeView format that DbgHelp resolves natively into the dump.
# Compile-side adds debug info, link-side asks the linker to keep it
# in a separate .pdb sidecar file rather than discarding it.
# Debug builds only -- release builds don't ship PDBs (different
# stripping policy + smaller install).
if(MINGW AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(selva-oscura PRIVATE -gcodeview)
    target_link_options(selva-oscura PRIVATE -Wl,-pdb=)
    # Engine static lib + selva static lib must ALSO emit codeview
    # otherwise their frames stay as opaque addresses even though
    # the executable's own frames resolve. Applied here (rather than
    # in each subdir) so the policy lives next to the consumer.
    target_compile_options(engine PRIVATE -gcodeview)
    target_compile_options(selva-oscura-lib PRIVATE -gcodeview)
endif()

# Sync config + assets to the build output on every build so the exe finds
# config/tunables.json and assets/characters/humanoid_male/humanoid.glb at runtime.
# ALL custom target (not POST_BUILD) so file edits are picked up even when
# no .cpp changed; same pattern as prison-escape-game.
#
# Bake targets (selva-oscura-convert-attacks, selva-oscura-convert-humanoid-legacy)
# depend on this so they run AFTER sync. Without the order constraint,
# Ninja races them: freshly-baked .ozz gets clobbered by the older
# source-tree .ozz when sync runs second. Symptom: in-game animations
# don't match the rebaked clips even though the bake intermediate is
# correct.
# copy_directory historically fails on the GitHub Actions Ubuntu runner
# when the destination subtree already partially exists from a prior
# bake step (bake writes ozz files into the same tree this sync
# targets, and CMake's copy_directory chokes on overlapping writes).
# Falling back to a python-driven recursive copy avoids the broken
# cmake behavior; Python3 is already a required dep at configure time.
add_custom_target(selva-oscura-sync-config ALL
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync_dir.py
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:selva-oscura>/config
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync_dir.py
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:selva-oscura>/assets
    COMMENT "Syncing selva-oscura config + assets to build output..."
    VERBATIM
)

# ---------------------------------------------------------------------------
# The Effigie -- standalone character designer.
#
# Same engine, same selva-oscura-lib, same Appearance struct. NO
# world / gameplay / physics / audio / dialog / region init -- boots
# straight into a preview-only window. Sits at build/bin/selva-effigie/
# to keep its assets + logs separate from the game exe.
#
# Doctrinal shape: authoring tool that WRITES config/characters/*.json
# snapshots the game consumes at runtime. See src/effigie/main.cpp for
# the Phase 1 empty-shell boot; Phase 2+ lands the designer UI.
# ---------------------------------------------------------------------------
add_executable(selva-effigie WIN32 src/effigie/main.cpp)
target_link_libraries(selva-effigie PRIVATE selva-oscura-lib)
target_include_directories(selva-effigie SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor/stb)
set_target_properties(selva-effigie PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/selva-effigie)
if(MINGW)
    target_link_options(selva-effigie PRIVATE -static-libgcc -static-libstdc++)
endif()

# Effigie needs its OWN copy of config + assets in its bin dir since it
# runs from build/bin/selva-effigie/. Same python sync script the game
# uses; syncs the full config + assets tree so any subsystem load
# (Appearance sliders, hair registry, humanoid.glb, iris texture) just
# finds its data.
#
# Trades disk (double-copy of ~200MB) for zero-configure simplicity --
# no per-subsystem path indirection, no environment variable, no
# "which config dir am I reading" ambiguity. Dev-tool concern; ship
# concern is only the game exe.
add_custom_target(selva-effigie-sync-config ALL
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync_dir.py
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:selva-effigie>/config
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync_dir.py
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:selva-effigie>/assets
    # Overlay the game exe's asset dir on top -- pulls in baked
    # skeleton.ozz + per-clip .ozz files that the bake pipeline emits
    # into build/bin/selva-oscura/. Those artifacts never live in the
    # source tree (gitignored); the Effigie needs them to load the
    # humanoid skeleton and drive its idle clip. Depends on the game
    # exe so the bakes run first.
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync_dir.py
        $<TARGET_FILE_DIR:selva-oscura>/assets
        $<TARGET_FILE_DIR:selva-effigie>/assets
    COMMENT "Syncing selva-effigie config + assets to build output..."
    VERBATIM
)
# Order: the game exe's bake steps must run first so .ozz files exist.
add_dependencies(selva-effigie-sync-config selva-oscura)

# ---------------------------------------------------------------------------
# Per-rig bake pipeline. Each character skeleton lives under
# assets/characters/<rig>/ with humanoid.glb as the authoring artifact +
# skeleton.ozz as the runtime extract.
#
# **humanoid.glb is produced by gen_humanoid.py (Blender extension),
# NOT by this build.** Earlier this pipeline ran fbx2gltf on the .fbx
# to produce .glb, but fbx2gltf strips glTF morph targets, breaking the
# AppearanceTransform / morph-slider system that requires them. gen_humanoid
# now writes humanoid.glb directly (preserving morphs through Blender's
# native glTF exporter) and the build just consumes it. To regen, run:
#
#     "/c/Program Files/Blender Foundation/Blender 4.2/blender.exe" \
#         --background --python \
#         games/selva-oscura/scripts/blender/gen_humanoid.py -- \
#         --gender male \
#         --out-blend assets/characters/humanoid_male/blender_authoring/humanoid_male.blend \
#         --out-fbx assets/characters/humanoid_male/source/rig/humanoid.fbx \
#         --out-glb assets/characters/humanoid_male/humanoid.glb \
#         --skin-diffuse assets/characters/humanoid_male/skin_diffuse.png \
#         --eyes-mhclo assets/characters/iris/high-poly.mhclo \
#         --eyes-diffuse assets/characters/iris/iris_diffuse.png
#
# The --eyes-mhclo + --eyes-diffuse pair is REQUIRED for a functional
# humanoid; without them the bake writes a .glb with no eye asset
# (single primitive, no HumanoidEyes material), and the eye submesh
# is entirely absent from the rendered character. There is no
# graceful degradation for this omission -- the character will render
# as an eyeless body.
#
# (same for --gender female, swapping paths). The .fbx is still written
# for Mixamo upload + animation round-trips, but the runtime mesh path
# is .glb only. Every clip must target exactly one rig -- mismatched
# skeletons distort geometry (see memory/feedback_no_bandaids_root_cause.md).
# ---------------------------------------------------------------------------
# Per-game build output (see selva-oscura exe's RUNTIME_OUTPUT_DIRECTORY).
# add_custom_command's OUTPUT can't contain generator expressions, so this
# is a literal path -- if the per-game subdir name changes, update both.
set(selva_out_dir ${CMAKE_BINARY_DIR}/bin/selva-oscura)

# Bake one rig's FBX → glb → skeleton.ozz, exposing a
# selva-oscura-convert-<rig> target downstream rules can depend on. Sets
# parent-scope <rig>_dir / <rig>_out_dir / <rig>_glb so attack-pack bake
# code can reference per-rig paths without re-deriving them.
#
# COMPUTE_NORMALS arg: pass "always" for FBX sources that ship without
# per-vertex normals; pass "missing" or omit for sources that include
# them. Without the right setting the glb has no NORMAL attribute and
# our loader rejects the mesh as invalid.
function(_selva_register_skeleton)
    set(options "")
    set(one_value_args NAME COMPUTE_NORMALS)
    set(multi_value_args "")
    cmake_parse_arguments(RIG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
    if(NOT RIG_NAME)
        message(FATAL_ERROR "_selva_register_skeleton: NAME is required")
    endif()
    if(NOT RIG_COMPUTE_NORMALS)
        set(RIG_COMPUTE_NORMALS "missing")
    endif()

    set(rig_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/characters/${RIG_NAME})
    set(rig_out_dir ${selva_out_dir}/assets/characters/${RIG_NAME})
    set(rig_glb ${rig_dir}/humanoid.glb)
    set(rig_target selva-oscura-convert-${RIG_NAME})

    # COMPUTE_NORMALS was a legacy fbx2gltf-pipeline flag. gen_humanoid's
    # Blender exporter always writes normals, so the knob is dead. Kept
    # in the function signature for caller-compat; silenced here.
    set(_unused ${RIG_COMPUTE_NORMALS})

    if(EXISTS ${rig_glb})
        # Skeleton-only gltf2ozz config: humanoid.glb ships with no real
        # animations (just a degenerate T-pose), so empty "animations": []
        # skips the animation-validation step that would otherwise fail
        # the build despite the skeleton extracting successfully.
        set(rig_skeleton_config ${CMAKE_BINARY_DIR}/selva-oscura-${RIG_NAME}/skeleton_only.json)
        file(WRITE ${rig_skeleton_config}
        "{\n"
        "  \"skeleton\": { \"filename\": \"skeleton.ozz\", \"import\": { \"enable\": true } },\n"
        "  \"animations\": []\n"
        "}\n"
        )

        add_custom_command(
            OUTPUT ${rig_out_dir}/skeleton.ozz
            COMMAND ${CMAKE_COMMAND} -E make_directory ${rig_out_dir}
            # Normalize glb scale BEFORE gltf2ozz reads it. Some FBX
            # exports ship with a 100x scale on the Armature parent
            # node (cm-to-m baked into the hierarchy instead of the
            # vertex data). Left alone, the 100x lands on the root
            # joint's rest pose and every actor renders 100x larger
            # at runtime. Bake the scale into static translations so
            # skeleton.ozz comes out unit-scale.
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/normalize_glb_scale.py
                --inplace ${rig_glb}
            COMMAND ${CMAKE_COMMAND} -E chdir ${rig_out_dir}
                $<TARGET_FILE:gltf2ozz>
                --file=${rig_glb}
                --config_file=${rig_skeleton_config}
            DEPENDS
                ${rig_glb}
                gltf2ozz
                ${rig_skeleton_config}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/normalize_glb_scale.py
            COMMENT "Extracting ${RIG_NAME} skeleton to ozz runtime format..."
            VERBATIM
        )
        add_custom_target(${rig_target} ALL DEPENDS ${rig_out_dir}/skeleton.ozz)
        # Bake runs AFTER sync so freshly-baked skeleton.ozz isn't clobbered.
        add_dependencies(${rig_target} selva-oscura-sync-config)
    else()
        message(STATUS "selva-oscura: ${RIG_NAME}/humanoid.glb not found; using committed skeleton.ozz")
        # Downstream attack-pack / per-character bake rules declare
        # ${rig_out_dir}/skeleton.ozz as a DEPENDS input. Without an FBX
        # bake producing it, no CMake target satisfies that path on CI.
        # sync-config copies the committed source-tree skeleton.ozz to
        # the out_dir as a side-effect of its tree copy, but doesn't name
        # skeleton.ozz as an OUTPUT -- ninja's graph has no edge linking
        # downstream rules to sync-config's actual production. Custom
        # command with explicit OUTPUT + explicit copy fixes the graph.
        set(rig_src_skeleton ${rig_dir}/skeleton.ozz)
        add_custom_command(
            OUTPUT ${rig_out_dir}/skeleton.ozz
            COMMAND ${CMAKE_COMMAND} -E make_directory ${rig_out_dir}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${rig_src_skeleton} ${rig_out_dir}/skeleton.ozz
            DEPENDS ${rig_src_skeleton}
            COMMENT "Copying committed ${RIG_NAME} skeleton.ozz to build output..."
            VERBATIM
        )
        add_custom_target(${rig_target} ALL DEPENDS ${rig_out_dir}/skeleton.ozz)
        add_dependencies(${rig_target} selva-oscura-sync-config)
    endif()

    # Expose per-rig paths to parent scope so attack-pack bake code can
    # reference ${<RIG_NAME>_dir} / ${<RIG_NAME>_out_dir} / ${<RIG_NAME>_glb}
    # without re-deriving them.
    set(${RIG_NAME}_dir ${rig_dir} PARENT_SCOPE)
    set(${RIG_NAME}_out_dir ${rig_out_dir} PARENT_SCOPE)
    set(${RIG_NAME}_glb ${rig_glb} PARENT_SCOPE)
endfunction()

# Minimal bake for rigs sourced from Blender's native glTF exporter.
# Source layout: assets/characters/<name>/source/rig/humanoid.glb.
# Two steps: stage the .glb into the committed location, then run
# gltf2ozz to extract skeleton.ozz. No FBX2glTF, no scale normalize,
# no Python -- Blender's exporter respects scene units and produces
# a single-skin glb directly.
function(_selva_register_humanoid)
    set(one_value_args NAME)
    cmake_parse_arguments(RIG "" "${one_value_args}" "" ${ARGN})
    if(NOT RIG_NAME)
        message(FATAL_ERROR "_selva_register_humanoid: NAME is required")
    endif()

    set(rig_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/characters/${RIG_NAME})
    set(rig_out_dir ${selva_out_dir}/assets/characters/${RIG_NAME})
    set(rig_src_glb ${rig_dir}/source/rig/humanoid.glb)
    set(rig_glb ${rig_dir}/humanoid.glb)
    set(rig_target selva-oscura-convert-${RIG_NAME})

    set(rig_skeleton_config ${CMAKE_BINARY_DIR}/selva-oscura-${RIG_NAME}/skeleton_only.json)
    file(WRITE ${rig_skeleton_config}
    "{\n"
    "  \"skeleton\": { \"filename\": \"skeleton.ozz\", \"import\": { \"enable\": true } },\n"
    "  \"animations\": []\n"
    "}\n"
    )

    if(EXISTS ${rig_src_glb})
        add_custom_command(
            OUTPUT ${rig_glb}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${rig_src_glb} ${rig_glb}
            DEPENDS ${rig_src_glb}
            COMMENT "Staging ${RIG_NAME}/humanoid.glb..."
            VERBATIM
        )
        add_custom_command(
            OUTPUT ${rig_out_dir}/skeleton.ozz
            COMMAND ${CMAKE_COMMAND} -E make_directory ${rig_out_dir}
            COMMAND ${CMAKE_COMMAND} -E chdir ${rig_out_dir}
                $<TARGET_FILE:gltf2ozz>
                --file=${rig_glb}
                --config_file=${rig_skeleton_config}
            DEPENDS ${rig_glb} gltf2ozz ${rig_skeleton_config}
            COMMENT "Extracting ${RIG_NAME} skeleton.ozz..."
            VERBATIM
        )
        # Per-clip bake: any .glb under source/clips/ becomes a per-clip
        # .ozz against this rig's skeleton. Retargeting + export was
        # done in Blender (via scripts/blender/retarget_mixamo.py); the
        # script writes .glb directly so this step is just gltf2ozz.
        # No FBX2glTF, no scale-normalize -- the .glb is already on the
        # target skeleton.
        set(rig_clip_ozz_files "")
        file(GLOB_RECURSE rig_clip_glb_files
            CONFIGURE_DEPENDS
            "${rig_dir}/source/clips/*.glb"
        )
        set(rig_clip_config ${CMAKE_BINARY_DIR}/selva-oscura-${RIG_NAME}/clip_only.json)
        file(WRITE ${rig_clip_config}
        "{\n"
        "  \"skeleton\": { \"filename\": \"${rig_out_dir}/skeleton.ozz\", \"import\": { \"enable\": false } }\n"
        "}\n"
        )
        if(rig_clip_glb_files)
            foreach(clip_glb ${rig_clip_glb_files})
                get_filename_component(clip_name ${clip_glb} NAME_WE)
                set(clip_temp_dir ${CMAKE_BINARY_DIR}/selva-oscura-${RIG_NAME}-clips/${clip_name})
                set(clip_final_ozz ${rig_out_dir}/${clip_name}.ozz)
                add_custom_command(
                    OUTPUT ${clip_final_ozz}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${clip_temp_dir}
                    COMMAND ${CMAKE_COMMAND} -E chdir ${clip_temp_dir}
                        $<TARGET_FILE:gltf2ozz>
                        --file=${clip_glb}
                        --config_file=${rig_clip_config}
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage_baked_ozz.py
                        --temp-dir ${clip_temp_dir}
                        --out ${clip_final_ozz}
                    DEPENDS
                        ${clip_glb}
                        gltf2ozz
                        ${rig_clip_config}
                        ${rig_out_dir}/skeleton.ozz
                    COMMENT "Baking ${RIG_NAME}/${clip_name}.glb -> ${clip_name}.ozz"
                    VERBATIM
                )
                list(APPEND rig_clip_ozz_files ${clip_final_ozz})
            endforeach()
        endif()
        add_custom_target(${rig_target} ALL DEPENDS ${rig_out_dir}/skeleton.ozz ${rig_clip_ozz_files})
        add_dependencies(${rig_target} selva-oscura-sync-config)
    else()
        message(STATUS "selva-oscura: ${RIG_NAME}/source/rig/humanoid.glb not found; using committed .glb + .ozz")
        set(rig_src_skeleton ${rig_dir}/skeleton.ozz)
        add_custom_command(
            OUTPUT ${rig_out_dir}/skeleton.ozz
            COMMAND ${CMAKE_COMMAND} -E make_directory ${rig_out_dir}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${rig_src_skeleton} ${rig_out_dir}/skeleton.ozz
            DEPENDS ${rig_src_skeleton}
            COMMENT "Copying committed ${RIG_NAME} skeleton.ozz..."
            VERBATIM
        )
        add_custom_target(${rig_target} ALL DEPENDS ${rig_out_dir}/skeleton.ozz)
        add_dependencies(${rig_target} selva-oscura-sync-config)
    endif()
endfunction()

# Parametric humanoid rigs -- canvas substrate every damned soul
# (player + NPCs + larvae) is built from. Player creator surfaces
# them as "Body Type 1 / 2".
_selva_register_humanoid(NAME humanoid_male)
_selva_register_humanoid(NAME humanoid_female)


# ---------------------------------------------------------------------------
# Game tests — pure-compute logic that doesn't need a GL context (animation
# loading, weapon data, dodge state machine math). Systems that need
# SDL/GL are tested by running the game itself.
# ---------------------------------------------------------------------------
add_executable(selva-oscura-tests
    tests/clip_geometry_test.cpp
    tests/skeletal_loader_test.cpp
    tests/spawn_flow_test.cpp
    tests/weapon_data_test.cpp
    tests/save_manager_test.cpp
    tests/camera_raycast_test.cpp
    tests/camera_physics_primitives_test.cpp
    tests/region_schema_test.cpp
    tests/boss_archetype_test.cpp
    tests/boss_dispatch_idempotence_test.cpp
    tests/boss_trigger_test.cpp
    tests/class_modifiers_test.cpp
    tests/identity_stat_test.cpp
    tests/player_class_test.cpp
    tests/roman_numeral_test.cpp
    tests/roman_tiers_test.cpp
    tests/sangue_pulse_test.cpp
    tests/sangue_test.cpp
    tests/soft_cap_test.cpp
    tests/territory_clamp_form_gate_test.cpp
    tests/weapon_damage_test.cpp
    tests/equipment_bridge_test.cpp
    tests/hand_cycle_test.cpp
    tests/hand_cycle_integration_test.cpp
    tests/quick_slot_test.cpp
    tests/heal_handler_test.cpp
    tests/appearance_test.cpp
    tests/authored_character_test.cpp
    tests/use_handlers_test.cpp
    tests/recipe_load_test.cpp
    tests/directional_loco_test.cpp
    tests/grip_transform_test.cpp
)
target_link_libraries(selva-oscura-tests PRIVATE selva-oscura-lib Catch2::Catch2WithMain)
set_target_properties(selva-oscura-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/selva-oscura)

# Run the test exe from the per-game build dir so relative paths in
# skeletal_loader_test.cpp ("assets/characters/...") resolve to the .ozz
# files produced by the gltf2ozz custom command. Also make the test depend
# on the conversion targets so the .ozz files exist by the time it runs.
add_test(
    NAME selva-oscura-tests
    COMMAND selva-oscura-tests
    WORKING_DIRECTORY $<TARGET_FILE_DIR:selva-oscura-tests>
)
if(TARGET selva-oscura-convert-humanoid-legacy)
    add_dependencies(selva-oscura-tests selva-oscura-convert-humanoid-legacy)
endif()
if(TARGET selva-oscura-convert-humanoid_male)
    add_dependencies(selva-oscura-tests selva-oscura-convert-humanoid_male)
endif()
if(TARGET selva-oscura-convert-humanoid_female)
    add_dependencies(selva-oscura-tests selva-oscura-convert-humanoid_female)
endif()
if(TARGET selva-oscura-convert-attacks)
    add_dependencies(selva-oscura-tests selva-oscura-convert-attacks)
endif()

# ---------------------------------------------------------------------------
# dump_world — headless build-time tool. Links the engine + game lib,
# runs registerAuthoredWorld(), serializes the StructureFootprint
# registry to JSON. Build-time tools (Python heightmap baker, etc.)
# consume the generated JSON to mirror the runtime registry without
# linking the engine. See games/selva-oscura/tools/dump_world.cpp.
# ---------------------------------------------------------------------------
add_executable(selva-oscura-dump-world tools/dump_world.cpp)
target_link_libraries(selva-oscura-dump-world PRIVATE selva-oscura-lib)
if(MINGW)
    target_link_options(selva-oscura-dump-world PRIVATE -static-libgcc -static-libstdc++)
endif()

# Bake step: invoke dump-world to produce the generated world-registry
# JSON, then run the heightmap baker against it. Wiring this as a
# build target means iteration is a single command (cmake --build
# build --target selva-oscura-bake-terrain); whoever changes a
# structure footprint just rebuilds and the heightmap regenerates.
#
# Output path is gitignored: assets/world/terrain/_world_registry.generated.json
# (the underscore + .generated.json suffix marks it as derived).
set(world_registry_json
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/world/terrain/_world_registry.generated.json
)
add_custom_command(
    OUTPUT ${world_registry_json}
    COMMAND $<TARGET_FILE:selva-oscura-dump-world> --out ${world_registry_json}
    DEPENDS selva-oscura-dump-world
    COMMENT "Dumping world structure registry → _world_registry.generated.json"
    VERBATIM
)

# Python heightmap baker — depends on the generated registry so any
# structure-footprint change in C++ propagates to the heightmap PNGs
# on the next build. The bake script reads the JSON and applies
# cuts_rim to the disc-rim heightmap (rim stays at floor Y inside
# any cuts_rim footprint).
#
# OUTPUT names the terrain region PNGs so ninja tracks them as
# generated artifacts; missing/stale PNG triggers a rebake.
set(terrain_heightmap_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/world/terrain)
set(terrain_heightmap_pngs
    ${terrain_heightmap_dir}/selva_inner.png
    ${terrain_heightmap_dir}/limbo.png
)
add_custom_command(
    OUTPUT ${terrain_heightmap_pngs}
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_terrain_heightmap.py
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_terrain_heightmap.py
        ${world_registry_json}
    COMMENT "Baking terrain heightmaps (uses generated world registry)"
    VERBATIM
)
add_custom_target(selva-oscura-bake-terrain ALL DEPENDS ${terrain_heightmap_pngs})
# Heightmap PNGs are sync'd into build/bin/assets/ by selva-oscura-sync-config.
# Run bake BEFORE sync so freshly-baked PNGs make it into the deployed assets.
add_dependencies(selva-oscura-sync-config selva-oscura-bake-terrain)
