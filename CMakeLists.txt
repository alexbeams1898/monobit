cmake_minimum_required(VERSION 3.20)
project(PrisonBreakGame VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json — used by clang-tidy and VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Predictable output locations
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(cmake/FetchDependencies.cmake)

add_subdirectory(engine)
add_subdirectory(game)

enable_testing()
add_subdirectory(tests)
