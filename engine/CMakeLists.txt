add_library(engine STATIC)

target_sources(engine PRIVATE
    src/Engine.cpp
    # Systems, components, and other engine sources added here as they are built
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
)

# Mark SDL2 headers as system includes so clang-tidy ignores their internals.
# Without this, bugprone-* and other checks fire on SDL2's own macros/typedefs.
get_target_property(_SDL2_INCLUDES SDL2::SDL2-static INTERFACE_INCLUDE_DIRECTORIES)
if(_SDL2_INCLUDES)
    target_include_directories(engine SYSTEM PRIVATE ${_SDL2_INCLUDES})
endif()

target_compile_features(engine PUBLIC cxx_std_17)
