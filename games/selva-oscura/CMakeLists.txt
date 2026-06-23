# Selva Oscura — 3D soulslike running on the main engine. Visual identity:
# pure 3D meshes with 1-bit shader treatment (locked 2026-05-05); see
# engines/engine/docs/3D-EXTENSION.md for the architectural plan.

# Per-game project declaration -- the release pipeline reads this version
# when a PR with prefix <bump>/selva-oscura/... merges. Starts at 0.0.0;
# the first <bump>/selva-oscura/... merge advances it from there.
project(SelvaOscura VERSION 0.3.0 LANGUAGES C CXX)

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
    src/gameplay/AppearanceDeformation.cpp
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
set_target_properties(selva-oscura PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/selva-oscura)

# Static-link the MinGW runtime so the exe runs without libgcc/libstdc++ DLLs.
# Mirrors the same option on prison-break-game; needed for distributable builds.
if(MINGW)
    target_link_options(selva-oscura PRIVATE -static-libgcc -static-libstdc++)
endif()

# Sync config + assets to the build output on every build so the exe finds
# config/tunables.json and assets/characters/humanoid/humanoid.glb at runtime.
# ALL custom target (not POST_BUILD) so file edits are picked up even when
# no .cpp changed; same pattern as prison-escape-game.
#
# Bake targets (selva-oscura-convert-attacks, selva-oscura-convert-humanoid)
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
# The humanoid is the canonical character rig -- standard Mixamo joint
# naming. All player + most enemy clips are authored against this
# skeleton. Choosing one shared skeleton means zero retargeting work --
# every Mixamo asset baked against it just plays.
#
# A previous incarnation used a custom Three.js Soldier mesh which
# *looked* like a Mixamo character but had a different bind pose and
# different units (centimeters vs glTF's meters). That mismatch made
# every imported clip distort the geometry. The fix isn't to translate
# between mismatched skeletons — it's to make sure there's only one.
# See memory/feedback_no_bandaids_root_cause.md.
# ---------------------------------------------------------------------------
# NOTE: this entire humanoid bake pipeline (lines ~145-405) will be
# factored into a `_selva_register_skeleton(id, source_dir, mesh_filename)`
# function when the WOLF skeleton lands in Phase B of the multi-skeleton
# refactor (see docs/design/animals_and_multi_skeleton.md). The wolf's
# bake follows the same shape: per-rig fbx2gltf -> per-rig gltf2ozz
# with that rig's skeleton as retarget reference -> per-clip gltf2ozz
# against the rig. Refactoring before the second skeleton exists is
# speculative (can't test the function-call shape without two callers);
# done concurrently with the wolf so both shape + behavior are verified
# in the same change. The PoseSampler / SkeletalAssets / EnemyArchetype
# C++ changes are already wolf-ready -- only the bake pipeline still
# assumes one skeleton, and that's pure mechanical CMake rearrangement.
set(humanoid_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/characters/humanoid)
set(fbx2gltf_exe ${CMAKE_SOURCE_DIR}/tools/fbx2gltf/FBX2glTF.exe)

# Per-game build output (see selva-oscura exe's RUNTIME_OUTPUT_DIRECTORY).
# add_custom_command's OUTPUT can't contain generator expressions, so this
# is a literal path -- if the per-game subdir name changes, update both.
set(selva_out_dir ${CMAKE_BINARY_DIR}/bin/selva-oscura)
set(humanoid_out_dir ${selva_out_dir}/assets/characters/humanoid)

# 1. humanoid.fbx → humanoid.glb. Mixamo's FBX export omits explicit per-vertex
# normals (Mixamo's pipeline assumes the consumer derives them), so we
# pass --compute-normals missing to FBX2glTF — without that flag, the
# resulting glb has no NORMAL attribute and our loader rejects the mesh
# as invalid. Doing the computation here means the runtime always sees a
# fully-attributed mesh — no special-case at load time.
#
# humanoid.glb is treated as a build artifact: regenerated from the FBX on
# fresh local checkouts (FBX is the authoring source), but the baked
# .glb + skeleton.ozz + per-clip .ozz files are also committed in the
# asset tree so CI / contributors without the gitignored Mixamo FBX
# pack can build and run. The FBX-bake pipeline below is wrapped in
# `if(EXISTS humanoid_fbx)` — when the FBX is present locally, the bake
# regenerates artifacts; when absent (CI), the committed bakes are used.
set(humanoid_fbx ${humanoid_dir}/source/rig/humanoid.fbx)
set(humanoid_glb ${humanoid_dir}/humanoid.glb)
if(EXISTS ${humanoid_fbx})
    add_custom_command(
        OUTPUT ${humanoid_glb}
        # FBX2glTF's --keep-attribute auto drops NORMAL and TEXCOORD if they
        # aren't strictly required by the schema. Mixamo characters ship
        # with both, but auto-mode strips them — the resulting glb has only
        # POSITION/JOINTS/WEIGHTS, which our loader rejects (no NORMAL).
        # Listing the attributes explicitly forces FBX2glTF to keep them.
        # --compute-normals always re-derives normals from geometry to be
        # safe (any FBX-export artifact gets cleaned up).
        COMMAND ${fbx2gltf_exe}
            --binary
            --keep-attribute position
            --keep-attribute normal
            --keep-attribute uv0
            --compute-normals always
            -i ${humanoid_fbx}
            -o ${humanoid_dir}/humanoid
        DEPENDS ${humanoid_fbx} ${fbx2gltf_exe}
        COMMENT "Converting humanoid.fbx → humanoid.glb..."
        VERBATIM
    )

    # 2. Skeleton-only conversion config: humanoid.glb ships with no real
    # animations (just a degenerate "Take 001" T-pose), so explicitly tell
    # gltf2ozz to skip animation extraction. Without "animations": [],
    # gltf2ozz fails its animation-validation step and exits non-zero — even
    # though the skeleton was already written successfully — making ninja
    # think the build step failed.
    set(humanoid_skeleton_config ${CMAKE_BINARY_DIR}/selva-oscura-humanoid/skeleton_only.json)
    file(WRITE ${humanoid_skeleton_config}
    "{\n"
    "  \"skeleton\": { \"filename\": \"skeleton.ozz\", \"import\": { \"enable\": true } },\n"
    "  \"animations\": []\n"
    "}\n"
    )

    # 3. Extract the skeleton from humanoid.glb.
    set(humanoid_ozz_files
        ${humanoid_out_dir}/skeleton.ozz
    )
    add_custom_command(
        OUTPUT ${humanoid_ozz_files}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${humanoid_out_dir}
        # Normalize the rig glb scale BEFORE gltf2ozz reads it. The
        # MB-Lab+auto-rigger humanoid.fbx ships with a 100x scale on
        # the Armature parent node (cm-to-m unit conversion baked
        # into the hierarchy instead of the vertex data). gltf2ozz
        # extracts skeleton.ozz INCLUDING that 100x scale on the
        # root joint's rest pose, which then propagates to every
        # bone palette at runtime (every actor renders 100x). We
        # bake the scale into the static translations so skeleton.ozz
        # comes out unit-scale.
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/normalize_glb_scale.py
            --inplace ${humanoid_glb}
        COMMAND ${CMAKE_COMMAND} -E chdir ${humanoid_out_dir}
            $<TARGET_FILE:gltf2ozz>
            --file=${humanoid_glb}
            --config_file=${humanoid_skeleton_config}
        DEPENDS
            ${humanoid_glb}
            gltf2ozz
            ${humanoid_skeleton_config}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/normalize_glb_scale.py
        COMMENT "Extracting humanoid skeleton to ozz runtime format..."
        VERBATIM
    )
    add_custom_target(selva-oscura-convert-humanoid ALL DEPENDS ${humanoid_ozz_files})
    # Bake runs AFTER sync so freshly-baked skeleton.ozz isn't clobbered.
    add_dependencies(selva-oscura-convert-humanoid selva-oscura-sync-config)
else()
    message(STATUS "selva-oscura: humanoid.fbx not found; skipping FBX bake (using committed .glb + .ozz)")
    # Downstream bake rules (sword pack, larva) declare
    # ${humanoid_out_dir}/skeleton.ozz as a DEPENDS input. Without an FBX
    # bake to produce it, no CMake target satisfies that path on CI.
    # sync-config already copies the committed source-tree skeleton.ozz
    # to ${humanoid_out_dir}/skeleton.ozz as a side-effect of its tree
    # copy, but it doesn't name skeleton.ozz as an OUTPUT, so ninja's
    # graph has no edge linking the sword/larva rules to sync-config's
    # actual production of that file. The fix: a custom command whose
    # OUTPUT is the file (so the graph is valid) and whose body
    # explicitly copies it from source (so it works even if sync-config
    # is reordered relative to it).
    set(humanoid_src_skeleton ${humanoid_dir}/skeleton.ozz)
    add_custom_command(
        OUTPUT ${humanoid_out_dir}/skeleton.ozz
        COMMAND ${CMAKE_COMMAND} -E make_directory ${humanoid_out_dir}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${humanoid_src_skeleton} ${humanoid_out_dir}/skeleton.ozz
        DEPENDS ${humanoid_src_skeleton}
        COMMENT "Copying committed humanoid skeleton.ozz to build output..."
        VERBATIM
    )
    add_custom_target(selva-oscura-convert-humanoid ALL DEPENDS ${humanoid_out_dir}/skeleton.ozz)
    add_dependencies(selva-oscura-convert-humanoid selva-oscura-sync-config)
endif()

# ---------------------------------------------------------------------------
# Convert Mixamo Pro Sword and Shield Pack FBXs → per-clip .ozz at build.
#
# Pipeline per FBX:
#   1. FBX2glTF converts the .fbx to a per-clip .glb in a build temp dir.
#   2. gltf2ozz runs with a config pointing at the humanoid skeleton.ozz
#      as the reference, with skeleton import disabled. The pack-extracted
#      glb has its own skeleton + an extra "RootNode" wrapper joint that
#      isn't in the humanoid skeleton; using the humanoid as the reference
#      means gltf2ozz strips that wrapper and indexes the animation tracks
#      against the humanoid's joint order. Result: clips play correctly on
#      the humanoid skeleton at runtime with no further retargeting.
#   3. The single animation track in each Mixamo FBX is named "mixamo.com"
#      internally — gltf2ozz emits it as "mixamo.com.ozz" regardless of
#      filename. We rename it to <sanitized-filename>.ozz alongside the
#      humanoid skeleton.ozz.
#
# The pack ships locomotion clips alongside attack clips, so no separate
# Idle/Walk/Run extraction is needed — sword_and_shield_idle/walk/run.ozz
# come from this same pipeline.
#
# Filename sanitization: lowercase, spaces and parens to underscores,
# repeats collapsed. "sword and shield slash (2).fbx" →
# "sword_and_shield_slash_2.ozz".
#
# Source layout under humanoid/source/ is purpose-based — every .fbx
# anywhere under that tree is converted to a clip, except the rig
# (rig/humanoid.fbx) which goes through a separate pipeline above. Drop
# new clips into the matching subdir (loco/, combat/sword/attacks/,
# dodge/, etc.) and they're picked up automatically.
#
# Override convention: clips under source/authored/ win by sanitized
# name over same-named clips elsewhere. Lets us replace Mixamo
# downloads with hand-authored MB-Lab versions one at a time; the
# rest keep using the Mixamo fallback.
# ---------------------------------------------------------------------------

# Generate the gltf2ozz retarget config once at configure time. Refers to
# the humanoid skeleton.ozz by absolute path so each clip's per-temp-dir
# gltf2ozz invocation can find it regardless of cwd.
#
# The "motion" block extracts root motion (hip translation/rotation) out
# of the animation tracks and bakes it back in as zero — i.e. the
# imported clip animates "in place" with the character staying at origin
# and only rotation/swing motion preserved on the joints. Mixamo's
# walk/run clips ship with hip translation that drives the character
# forward across the clip; without this extraction, the animation
# moves the character forward each loop and snaps back at wrap, while
# our gameplay code is also driving sPlayer.pos via WASD — total
# duplication, looks like skipping.
#
# bake=true means "consume the motion delta and re-zero the animation
# track." We drop the extracted-motion track files (filename references
# a tmp scratch path) since gameplay drives translation, not animation.
set(retarget_config ${CMAKE_BINARY_DIR}/selva-oscura-attacks/retarget_config.json)
file(WRITE ${retarget_config}
"{\n"
"  \"skeleton\": {\n"
"    \"filename\": \"${humanoid_out_dir}/skeleton.ozz\",\n"
"    \"import\": { \"enable\": false }\n"
"  },\n"
"  \"animations\": [\n"
"    {\n"
"      \"clip\": \"*\",\n"
"      \"filename\": \"*.ozz\",\n"
"      \"additive\": false,\n"
"      \"optimize\": true\n"
"    }\n"
"  ]\n"
"}\n"
)

# Helper: turn a Mixamo filename into our sanitized clip name.
function(_selva_sanitize_clip_name fbx_filename out_var)
    get_filename_component(stem "${fbx_filename}" NAME_WE)
    string(TOLOWER "${stem}" sanitized)
    string(REGEX REPLACE "[^a-z0-9]+" "_" sanitized "${sanitized}")
    string(REGEX REPLACE "_+" "_" sanitized "${sanitized}")
    string(REGEX REPLACE "^_|_$" "" sanitized "${sanitized}")
    set(${out_var} "${sanitized}" PARENT_SCOPE)
endfunction()

set(attack_ozz_files "")
# Every .fbx anywhere under humanoid/source/ becomes a per-clip .ozz.
# Layout is purpose-based (loco/, combat/, dodge/, ...); recursive
# glob lets new subdirs work without listing them here. humanoid.fbx
# (the rig under rig/) is converted separately above into humanoid.glb
# and excluded by the filter below.
file(GLOB_RECURSE attack_fbx_files
    CONFIGURE_DEPENDS
    "${humanoid_dir}/source/*.fbx"
)
# Exclude rig FBXs: our humanoid.fbx in source/rig/, plus legacy
# "X Bot.fbx" / "X_Bot.fbx" rig mannequins shipped inside Mixamo
# packs (e.g. Creature Pack). These are T-pose mesh sources, not
# clips -- gltf2ozz fails to validate them as animations.
list(FILTER attack_fbx_files EXCLUDE REGEX ".*humanoid\\.fbx$|.*[Xx][ _]?[Bb]ot.*\\.fbx$")

# Authored override: any .fbx under source/authored/ wins by sanitized
# name over the same-named clip from elsewhere in source/. Lets us
# author MB-Lab clips that replace Mixamo equivalents one at a time;
# unauthored clips keep falling back to the Mixamo download.
set(_authored_clip_names "")
foreach(fbx ${attack_fbx_files})
    if(fbx MATCHES "/source/authored/")
        _selva_sanitize_clip_name("${fbx}" _name)
        list(APPEND _authored_clip_names ${_name})
    endif()
endforeach()
if(_authored_clip_names)
    set(_deduped_fbx "")
    foreach(fbx ${attack_fbx_files})
        _selva_sanitize_clip_name("${fbx}" _name)
        if(fbx MATCHES "/source/authored/")
            list(APPEND _deduped_fbx ${fbx})
        else()
            list(FIND _authored_clip_names ${_name} _idx)
            if(_idx EQUAL -1)
                list(APPEND _deduped_fbx ${fbx})
            endif()
        endif()
    endforeach()
    set(attack_fbx_files ${_deduped_fbx})
endif()
# Only run the sword-pack bake when FBX2glTF is available. On CI
# (Linux without the gitignored FBX2glTF.exe), the bake is skipped and
# the committed .ozz files in humanoid/ ship as the runtime artifacts.
if(attack_fbx_files AND EXISTS ${fbx2gltf_exe})
    foreach(fbx ${attack_fbx_files})
        _selva_sanitize_clip_name("${fbx}" clip_name)
        set(clip_temp_dir ${CMAKE_BINARY_DIR}/selva-oscura-attacks/${clip_name})
        set(clip_glb ${clip_temp_dir}/${clip_name}.glb)
        set(clip_final_ozz ${humanoid_out_dir}/${clip_name}.ozz)
        add_custom_command(
            OUTPUT ${clip_final_ozz}
            # 1. Convert FBX → glb (FBX2glTF -o is a stem; it appends .glb).
            COMMAND ${CMAKE_COMMAND} -E make_directory ${clip_temp_dir}
            COMMAND ${fbx2gltf_exe}
                --binary
                --keep-attribute auto
                -i ${fbx}
                -o ${clip_temp_dir}/${clip_name}
            # 2. Normalize glb scale -- Mixamo "With Skin" downloads
            #    against our MB-Lab+auto-rigger humanoid carry
            #    cm-magnitude bone translations + a 100x scale on the
            #    hips/mesh nodes. gltf2ozz strips per-joint scale but
            #    keeps the cm translations -> runtime renders 100x.
            #    This step bakes the 100x scale into the translation
            #    tracks so gltf2ozz sees meter-scale values. No-op on
            #    glbs without non-unit scale (idempotent / safe).
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/normalize_glb_scale.py
                --inplace ${clip_glb}
            # 3. gltf2ozz with the retarget config — skeleton import
            #    disabled, animation indexed against the humanoid skeleton.
            COMMAND ${CMAKE_COMMAND} -E chdir ${clip_temp_dir}
                $<TARGET_FILE:gltf2ozz>
                --file=${clip_glb}
                --config_file=${retarget_config}
            # 4. Stage the gltf2ozz output into the final name.
            #    gltf2ozz names output by the animation's internal name
            #    (mixamo.com.ozz for Mixamo, Scene.ozz for Blender-
            #    re-exports). stage_baked_ozz.py globs for whatever
            #    .ozz exists and copies it — names aren't load-bearing.
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage_baked_ozz.py
                --temp-dir ${clip_temp_dir}
                --out ${clip_final_ozz}
            DEPENDS
                ${fbx}
                gltf2ozz
                ${retarget_config}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/normalize_glb_scale.py
                # The retarget config refers to the humanoid skeleton.ozz —
                # it must exist before gltf2ozz reads it.
                ${humanoid_out_dir}/skeleton.ozz
            COMMENT "Converting ${clip_name}.fbx → ${clip_name}.ozz (retargeted to humanoid)"
            VERBATIM
        )
        list(APPEND attack_ozz_files ${clip_final_ozz})
    endforeach()
endif()

# Only define the target if there's at least one .fbx; otherwise CMake
# warns about empty custom targets. Missing pack is a soft failure — the
# game runs without attack clips, just with whatever JSON references stay
# unresolved.
if(attack_ozz_files)
    add_custom_target(selva-oscura-convert-attacks ALL DEPENDS ${attack_ozz_files})
    # Bake runs AFTER sync so freshly-baked .ozz isn't clobbered by the
    # source-tree's older .ozz (which copy_directory above re-deploys).
    add_dependencies(selva-oscura-convert-attacks selva-oscura-sync-config)
endif()

# ---------------------------------------------------------------------------
# Larva clip bake. Larvae are Hell-side contrapasso-shaped human souls
# per [[universal-humanoid-enemy-rule]] -- they reuse the humanoid
# skeleton, so this pipeline mirrors the humanoid pack bake above but
# globs a separate source root and deploys to a separate output dir.
# Source: assets/characters/larva/source/ (Mixamo Scary Zombie Pack +
# any future larva-specific clips). Same retarget_config (humanoid
# skeleton.ozz, skeleton import disabled) -- Mixamo zombie clips are
# bound to the standard Mixamo skeleton the humanoid uses, so retargeting
# Just Works. Output: bin/selva-oscura/assets/characters/larva/*.ozz.
# ---------------------------------------------------------------------------
set(larva_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/characters/larva)
set(larva_out_dir ${selva_out_dir}/assets/characters/larva)
set(larva_ozz_files "")
file(GLOB_RECURSE larva_fbx_files
    CONFIGURE_DEPENDS
    "${larva_dir}/source/*.fbx"
)
# Skip the Mixamo rig FBX shipped with the Scary Zombie Pack -- it's
# a T-pose source, not a clip. Matches "X Bot.fbx", "X_Bot.fbx",
# "x bot.fbx", etc. (legacy filenames from the original pack contents).
list(FILTER larva_fbx_files EXCLUDE REGEX ".*[Xx][ _][Bb]ot\\.fbx$")
# Only run the larva bake when FBX2glTF is available. On CI (Linux
# without the gitignored FBX2glTF.exe), the bake is skipped and the
# committed .ozz files in larva/ ship as the runtime artifacts.
if(larva_fbx_files AND EXISTS ${fbx2gltf_exe})
    foreach(fbx ${larva_fbx_files})
        _selva_sanitize_clip_name("${fbx}" clip_name)
        set(clip_temp_dir ${CMAKE_BINARY_DIR}/selva-oscura-larva/${clip_name})
        set(clip_glb ${clip_temp_dir}/${clip_name}.glb)
        set(clip_final_ozz ${larva_out_dir}/${clip_name}.ozz)
        add_custom_command(
            OUTPUT ${clip_final_ozz}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${clip_temp_dir}
            COMMAND ${fbx2gltf_exe}
                --binary
                --keep-attribute auto
                -i ${fbx}
                -o ${clip_temp_dir}/${clip_name}
            # Normalize glb scale (same fix as the humanoid pack -- see
            # comment there for the full story).
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/normalize_glb_scale.py
                --inplace ${clip_glb}
            COMMAND ${CMAKE_COMMAND} -E chdir ${clip_temp_dir}
                $<TARGET_FILE:gltf2ozz>
                --file=${clip_glb}
                --config_file=${retarget_config}
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage_baked_ozz.py
                --temp-dir ${clip_temp_dir}
                --out ${clip_final_ozz}
            DEPENDS
                ${fbx}
                gltf2ozz
                ${retarget_config}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/normalize_glb_scale.py
                ${humanoid_out_dir}/skeleton.ozz
            COMMENT "Converting ${clip_name}.fbx → larva/${clip_name}.ozz (retargeted to humanoid)"
            VERBATIM
        )
        list(APPEND larva_ozz_files ${clip_final_ozz})
    endforeach()
endif()
if(larva_ozz_files)
    add_custom_target(selva-oscura-convert-larva ALL DEPENDS ${larva_ozz_files})
    add_dependencies(selva-oscura-convert-larva selva-oscura-sync-config)
endif()

# Pre-baked .ozz files whose source FBXs are no longer in the tree
# (e.g. unused sword & shield clips kept available for future use).
# Copy each into build/bin alongside the freshly-baked clips so
# loadDirectory() picks them up at runtime. The runtime directory
# scan doesn't distinguish baked-from-FBX from archived.
#
# When a clip exists in BOTH archive AND source/, the FBX is now the
# source of truth -- skip the stale archive copy. Otherwise CMake
# errors on a duplicate custom-command output.
set(archive_dir ${humanoid_dir}/baked-archive)
if(EXISTS ${archive_dir})
    # Build the set of clip names that the source-FBX bake will produce.
    set(baked_clip_names "")
    foreach(fbx ${attack_fbx_files})
        _selva_sanitize_clip_name("${fbx}" clip_name)
        list(APPEND baked_clip_names ${clip_name})
    endforeach()

    file(GLOB archive_ozz_files CONFIGURE_DEPENDS "${archive_dir}/*.ozz")
    set(archive_copied_files "")
    foreach(src ${archive_ozz_files})
        get_filename_component(name "${src}" NAME)
        get_filename_component(stem "${src}" NAME_WE)
        if(stem IN_LIST baked_clip_names)
            continue()
        endif()
        set(dst ${humanoid_out_dir}/${name})
        add_custom_command(
            OUTPUT ${dst}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${humanoid_out_dir}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
            DEPENDS ${src}
            COMMENT "Staging archived clip ${name}"
            VERBATIM
        )
        list(APPEND archive_copied_files ${dst})
    endforeach()
    if(archive_copied_files)
        add_custom_target(selva-oscura-stage-archive ALL DEPENDS ${archive_copied_files})
        # Archived clips also stage AFTER sync, same anti-clobber reason.
        add_dependencies(selva-oscura-stage-archive selva-oscura-sync-config)
    endif()
endif()

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
if(TARGET selva-oscura-convert-humanoid)
    add_dependencies(selva-oscura-tests selva-oscura-convert-humanoid)
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
