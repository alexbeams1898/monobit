add_library(engine STATIC)

target_sources(engine PRIVATE
    src/engine.cpp
    # Systems, components, and other engine sources added here as they are built
)

target_include_directories(engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(engine
    PUBLIC
        SDL2::SDL2-static
        EnTT::EnTT
        nlohmann_json::nlohmann_json
        FMOD::Core
)

target_compile_features(engine PUBLIC cxx_std_17)
