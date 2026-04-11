add_library(engine STATIC)

target_sources(engine PRIVATE
    src/Engine.cpp
    src/TextureManager.cpp
    src/SpriteCompositor.cpp
    src/FontManager.cpp
    src/UIRenderer.cpp
    src/systems/RenderSystem.cpp
    src/systems/CameraSystem.cpp
    src/systems/CollisionSystem.cpp
    src/systems/AnimationSystem.cpp
    src/systems/AudioSystem.cpp
    src/TileMapLoader.cpp
    src/systems/TileMapRenderer.cpp
    src/systems/FlowFieldSystem.cpp
    src/systems/SteeringSystem.cpp
    src/systems/CameraPanSystem.cpp
    src/utils/DirectionUtils.cpp
    src/gl/ShaderUtils.cpp
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
        nlohmann_json::nlohmann_json
        FMOD::Core
        glad
        TracyClient
        miniaudio::miniaudio
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
# Engine tests -- only test engine code, no game dependency.
# ---------------------------------------------------------------------------
add_executable(engine-tests
    tests/ecs_test.cpp
    tests/collision_test.cpp
    tests/tilemap_test.cpp
    tests/animation_test.cpp
)
target_link_libraries(engine-tests PRIVATE engine Catch2::Catch2WithMain)

add_test(NAME engine-tests COMMAND engine-tests)
set_tests_properties(engine-tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/game)
