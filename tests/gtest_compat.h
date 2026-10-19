// Minimal test runner — used when GoogleTest is not installed.
// Provides a GTest-compatible subset of macros so tests compile unchanged.
#pragma once
#include <iostream>
#include <sstream>
#include <vector>
#include <functional>
#include <string>
#include <stdexcept>
#include <cstdlib>

// ── Registry ─────────────────────────────────────────────────────────────────
struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> v;
    return v;
}

struct TestRegistrar {
    TestRegistrar(const char* suite, const char* name, std::function<void()> fn) {
        test_registry().push_back({suite, name, std::move(fn)});
    }
};

// ── GTest-compatible macros ───────────────────────────────────────────────────
#define TEST(suite, name)                                                           \
    static void suite##_##name();                                                   \
    static TestRegistrar suite##_##name##_reg(#suite, #name, suite##_##name);       \
    static void suite##_##name()

#define EXPECT_EQ(a,b)  do { if (!((a)==(b))) {                                    \
    std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__                          \
              << "  EXPECT_EQ(" #a ", " #b ")\n"                                    \
              << "    lhs=" << (a) << "  rhs=" << (b) << "\n";                      \
    throw std::runtime_error("assertion failed");                                   \
}} while(0)

#define EXPECT_NE(a,b)  do { if ((a)==(b)) {                                        \
    std::cerr << "  FAIL " __FILE__ ":" << __LINE__ << " EXPECT_NE\n";             \
    throw std::runtime_error("assertion failed");                                   \
}} while(0)

#define EXPECT_TRUE(x)  do { if (!(x)) {                                            \
    std::cerr << "  FAIL " __FILE__ ":" << __LINE__                                 \
              << " EXPECT_TRUE(" #x ")\n";                                          \
    throw std::runtime_error("assertion failed");                                   \
}} while(0)

#define EXPECT_FALSE(x) EXPECT_TRUE(!(x))

#define EXPECT_LT(a,b)  do { if (!((a)<(b))) {                                      \
    std::cerr << "  FAIL " __FILE__ ":" << __LINE__                                 \
              << " EXPECT_LT " << (a) << " < " << (b) << "\n";                     \
    throw std::runtime_error("assertion failed");                                   \
}} while(0)

#define EXPECT_LE(a,b)  do { if (!((a)<=(b))) {                                     \
    std::cerr << "  FAIL " __FILE__ ":" << __LINE__                                 \
              << " EXPECT_LE " << (a) << " <= " << (b) << "\n";                    \
    throw std::runtime_error("assertion failed");                                   \
}} while(0)

#define EXPECT_GE(a,b)  do { if (!((a)>=(b))) {                                     \
    std::cerr << "  FAIL " __FILE__ ":" << __LINE__                                 \
              << " EXPECT_GE " << (a) << " >= " << (b) << "\n";                    \
    throw std::runtime_error("assertion failed");                                   \
}} while(0)

#define ASSERT_EQ(a,b)  EXPECT_EQ(a,b)
#define ASSERT_TRUE(x)  EXPECT_TRUE(x)
#define ASSERT_NE(a,b)  EXPECT_NE(a,b)

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    int passed = 0, failed = 0;
    for (auto& tc : test_registry()) {
        std::cout << "[ RUN      ] " << tc.suite << "." << tc.name << "\n";
        try {
            tc.fn();
            std::cout << "[       OK ] " << tc.suite << "." << tc.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cerr << "[  FAILED  ] " << tc.suite << "." << tc.name
                      << " – " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << "\n[==========] " << (passed+failed) << " tests.\n"
              << "[  PASSED  ] " << passed << " tests.\n";
    if (failed) {
        std::cerr << "[  FAILED  ] " << failed << " tests.\n";
        return 1;
    }
    return 0;
}
