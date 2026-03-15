add_executable(engine-tests
    ecs_test.cpp
)

target_link_libraries(engine-tests
    PRIVATE
        engine
        Catch2::Catch2WithMain
)

# Mark Catch2 headers as system includes (same reason as SDL2 — avoids
# clang-tidy firing on Catch2 internals if tidy is ever run on tests/).
get_target_property(_CATCH2_INCLUDES Catch2::Catch2 INTERFACE_INCLUDE_DIRECTORIES)
if(_CATCH2_INCLUDES)
    target_include_directories(engine-tests SYSTEM PRIVATE ${_CATCH2_INCLUDES})
endif()

# Register with CTest so "cmake --build build --target test" runs everything.
add_test(NAME engine-tests COMMAND engine-tests)
