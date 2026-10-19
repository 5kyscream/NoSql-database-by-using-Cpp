cmake_minimum_required(VERSION 3.16)

# ── Bundled minimal test runner ───────────────────────────────────────────────
add_library(minired_testrunner INTERFACE)
target_include_directories(minired_testrunner INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
set(GTEST_LIBS minired_testrunner)

# ── Gather storage + protocol sources (excluding main.cpp) ───────────────────
file(GLOB_RECURSE LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/protocol/*.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/*.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/*.cpp
    ${CMAKE_SOURCE_DIR}/src/server/config.cpp
)

# Helper function to create a test executable
function(add_minired_test name src)
    add_executable(${name} ${src} ${LIB_SOURCES})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} ${GTEST_LIBS} pthread)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_minired_test(test_resp       test_resp.cpp)
add_minired_test(test_memtable   test_memtable.cpp)
add_minired_test(test_sstable    test_sstable.cpp)
add_minired_test(test_bloom      test_bloom.cpp)
add_minired_test(test_compaction test_compaction.cpp)
