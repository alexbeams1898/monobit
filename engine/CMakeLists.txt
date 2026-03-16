add_library(engine STATIC)

target_sources(engine PRIVATE
    src/Engine.cpp
    src/ConfigLoader.cpp
    src/TextureManager.cpp
    src/systems/InputSystem.cpp
    src/systems/MovementSystem.cpp
    src/systems/RenderSystem.cpp
    src/systems/CameraSystem.cpp
    src/systems/CollisionSystem.cpp
    src/systems/AggroSystem.cpp
    src/systems/ChaseSystem.cpp
    src/systems/FlowFieldSystem.cpp
    src/systems/SteeringSystem.cpp
    src/systems/SpawnerSystem.cpp
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
)

target_compile_features(engine PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# Mark third-party headers as SYSTEM includes so clang-tidy ignores their internals.
# ---------------------------------------------------------------------------

get_target_property(_SDL2_INCLUDES SDL2::SDL2-static INTERFACE_INCLUDE_DIRECTORIES)
if(_SDL2_INCLUDES)
    target_include_directories(engine SYSTEM PRIVATE ${_SDL2_INCLUDES})
endif()

get_target_property(_JSON_INCLUDES nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
if(_JSON_INCLUDES)
    target_include_directories(engine SYSTEM PRIVATE ${_JSON_INCLUDES})
endif()

get_target_property(_ENTT_INCLUDES EnTT::EnTT INTERFACE_INCLUDE_DIRECTORIES)
if(_ENTT_INCLUDES)
    target_include_directories(engine SYSTEM PUBLIC ${_ENTT_INCLUDES})
endif()

get_target_property(_GLAD_INCLUDES glad INTERFACE_INCLUDE_DIRECTORIES)
if(_GLAD_INCLUDES)
    target_include_directories(engine SYSTEM PUBLIC ${_GLAD_INCLUDES})
endif()

get_target_property(_TRACY_INCLUDES TracyClient INTERFACE_INCLUDE_DIRECTORIES)
if(_TRACY_INCLUDES)
    target_include_directories(engine SYSTEM PUBLIC ${_TRACY_INCLUDES})
endif()

# stb_image is a vendored single-header library. Marking it SYSTEM keeps
# clang-tidy from firing on its internals. Lives in vendor/stb/ so it is
# excluded from the find engine game ... clang-format check.
target_include_directories(engine SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor/stb)
