#include "apexhft/risk.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace {

using namespace apexhft;

Order make_order(std::uint64_t id, Side side,
                 std::uint64_t quantity, std::int64_t price) {
    return Order{OrderId{id}, side, OrderType::Limit,
                 Quantity{quantity}, PriceTicks{price}};
}

RiskManager make_risk() {
    return RiskManager{RiskLimits{10, 20, 90, 110}};
}

TEST(RiskManagerTest, AcceptsOrderWithinLimits) {
    const auto risk = make_risk();
    EXPECT_FALSE(risk.check(make_order(1, Side::Buy, 5, 100), 0));
}

TEST(RiskManagerTest, RejectsOversizedOrder) {
    const auto risk = make_risk();
    EXPECT_TRUE(risk.check(make_order(1, Side::Buy, 11, 100), 0));
}

TEST(RiskManagerTest, RejectsProjectedLongPositionBreach) {
    const auto risk = make_risk();
    EXPECT_TRUE(risk.check(make_order(1, Side::Buy, 5, 100), 18));
}

TEST(RiskManagerTest, RejectsProjectedShortPositionBreach) {
    const auto risk = make_risk();
    EXPECT_TRUE(risk.check(make_order(1, Side::Sell, 5, 100), -18));
}

TEST(RiskManagerTest, AllowsOrderThatReducesPosition) {
    const auto risk = make_risk();
    EXPECT_FALSE(risk.check(make_order(1, Side::Sell, 5, 100), 18));
}

TEST(RiskManagerTest, RejectsPriceOutsideConfiguredRange) {
    const auto risk = make_risk();
    EXPECT_TRUE(risk.check(make_order(1, Side::Buy, 1, 111), 0));
}

TEST(RiskManagerTest, RejectsPositionArithmeticOverflow) {
    const auto risk = make_risk();
    EXPECT_TRUE(risk.check(
        make_order(1, Side::Buy, 1, 100),
        std::numeric_limits<std::int64_t>::max()));
}

TEST(RiskManagerTest, RejectsInvalidLimits) {
    EXPECT_THROW(
        (RiskManager{RiskLimits{0, 20, 90, 110}}),
        std::invalid_argument);
}

}  // namespace
