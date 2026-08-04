# Per-engine project declaration. Engine bumps don't ship a release artifact
# (the engine has no exe), but the version still moves so we can identify
# engine-only changes in commit history and tag them if needed.
project(Engine VERSION 0.1.0 LANGUAGES C CXX)

add_library(engine STATIC)

target_sources(engine PRIVATE
    src/Engine.cpp
    src/log/Log.cpp
    src/TextureManager.cpp
    src/SpriteCompositor.cpp
    src/FontManager.cpp
    src/UIRenderer.cpp
    src/ecs/ConfigLoaders.cpp
    src/ops/InventoryOps.cpp
    src/ops/CraftingOps.cpp
    src/physics/PhysicsWorld.cpp
    src/world/Region.cpp
    src/world/Territory.cpp
    src/world/TerrainModifiers.cpp
    src/world/StructureFootprints.cpp
    src/world/Lights.cpp
    src/systems/RenderSystem.cpp
    src/systems/CameraSystem.cpp
    src/systems/CollisionSystem.cpp
    src/systems/AnimationSystem.cpp
    src/systems/AudioSystem.cpp
    src/systems/TileMapRenderer.cpp
    src/systems/FlowFieldSystem.cpp
    src/systems/SteeringSystem.cpp
    src/systems/CameraPanSystem.cpp
    src/utils/DirectionUtils.cpp
    src/utils/SaveFile.cpp
    src/gl/ShaderUtils.cpp
    src/gl/PixelRenderTarget.cpp
)

target_include_directories(engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(engine
    PUBLIC
        SDL2::SDL2-static
        OpenGL::GL
        EnTT::EnTT
        glm::glm
        nlohmann_json::nlohmann_json
        fmt::fmt
        FMOD::Core
        glad
        imgui::imgui
        TracyClient
        miniaudio::miniaudio
        Jolt
)

# miniaudio on Windows requires these system libs (WASAPI/WinMM/COM).
if(WIN32)
    target_link_libraries(engine PUBLIC winmm ole32)
endif()

target_compile_features(engine PUBLIC cxx_std_17)

# stb_image is a vendored single-header library. Marking it SYSTEM keeps
# clang-tidy from firing on its internals. Lives in vendor/stb/ so it is
# excluded from the find engine game ... clang-format check.
target_include_directories(engine SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor/stb)

# ---------------------------------------------------------------------------
# Engine tests -- only test engine code, no game dependency. Tests are
# self-contained: no external config files, no per-game asset paths.
# ---------------------------------------------------------------------------
add_executable(engine-tests
    tests/ecs_test.cpp
    tests/collision_test.cpp
    tests/tilemap_test.cpp
    tests/animation_test.cpp
    tests/direction_mirror_test.cpp
    tests/inventory_ops_test.cpp
    tests/crafting_ops_test.cpp
    tests/evolution_ops_test.cpp
    tests/discovery_test.cpp
    tests/blit_rect_test.cpp
    tests/save_file_test.cpp
)
target_link_libraries(engine-tests PRIVATE engine Catch2::Catch2WithMain)

add_test(NAME engine-tests COMMAND engine-tests)
