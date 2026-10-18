cmake_minimum_required(VERSION 3.16)
project(MiniRed VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── compiler flags ──────────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic -O2)

# ── include paths ────────────────────────────────────────────────────────────
include_directories(src)

# ── gather sources ───────────────────────────────────────────────────────────
file(GLOB_RECURSE SERVER_SOURCES
    src/protocol/*.cpp
    src/server/*.cpp
    src/storage/*.cpp
    src/commands/*.cpp
)

# ── main executable ──────────────────────────────────────────────────────────
add_executable(minired src/main.cpp ${SERVER_SOURCES})
target_link_libraries(minired pthread)

# ── internal benchmark ───────────────────────────────────────────────────────
add_executable(internal_bench
    benchmarks/internal_bench.cpp
    ${SERVER_SOURCES}
)
target_link_libraries(internal_bench pthread)

# ── tests ────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
