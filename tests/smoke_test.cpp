#include <gtest/gtest.h>

// Proves the toolchain (CMake + C++17 + GoogleTest via FetchContent) works
// end to end. No domain logic exists yet to test.
TEST(Smoke, ProjectCompiles) {
    EXPECT_TRUE(true);
}
