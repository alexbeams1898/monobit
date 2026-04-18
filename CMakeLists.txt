cmake_minimum_required(VERSION 3.25)
project(PrisonBreakGame VERSION 0.4.0 LANGUAGES C CXX)

# GLAD v0.1.36 declares cmake_minimum_required(VERSION 2.8), which CMake 4.x
# no longer accepts. This sets the effective minimum to 3.5 for all
# subdirectories/deps so GLAD configures cleanly on CMake 4.x.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

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

# ---------------------------------------------------------------------------
# Sanitizers  (disabled by default — enable with -DENABLE_SANITIZERS=ON)
# Catches memory errors (ASan) and undefined behaviour (UBSan) at runtime.
# Use for debug builds only; incompatible with MSVC. Requires GCC or Clang.
# ---------------------------------------------------------------------------
option(ENABLE_SANITIZERS "Build with AddressSanitizer + UBSan" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

add_subdirectory(engine)
add_subdirectory(game)
