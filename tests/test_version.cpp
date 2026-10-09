#include "apexhft/version.hpp"

#include <gtest/gtest.h>

#include <string_view>

TEST(VersionTest, ReturnsExpectedInitialVersion) {
    EXPECT_EQ(apexhft::version(), std::string_view{"0.1.0"});
}

TEST(VersionTest, IsNotEmpty) {
    EXPECT_FALSE(apexhft::version().empty());
}
