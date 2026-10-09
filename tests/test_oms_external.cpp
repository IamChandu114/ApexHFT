#include "apexhft/oms.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace apexhft {
namespace {

Order make_order(std::uint64_t id, Side side, std::uint64_t quantity,
                 std::int64_t price) {
    return Order{OrderId{id}, side, OrderType::Limit, Quantity{quantity},
                 PriceTicks{price}};
}

RiskLimits test_limits() {
    return RiskLimits{10, 10, 1, 1000};
}

TEST(OMSExternalTest, ExternalAskFillsOurBuy) {
    OrderManagementSystem oms{test_limits()};
    (void)oms.add_external_liquidity(make_order(100, Side::Sell, 5, 100));

    const auto result = oms.submit(make_order(1, Side::Buy, 3, 100));
    ASSERT_TRUE(result.accepted);
    ASSERT_EQ(result.trades.size(), 1U);
    EXPECT_EQ(oms.position(), 3);
    EXPECT_EQ(oms.get_order(OrderId{1}).status(), OrderStatus::Filled);
    EXPECT_EQ(oms.active_order_count(), 0U);
}

TEST(OMSExternalTest, ExternalBidFillsOurSell) {
    OrderManagementSystem oms{test_limits()};
    (void)oms.add_external_liquidity(make_order(100, Side::Buy, 5, 100));

    const auto result = oms.submit(make_order(1, Side::Sell, 2, 100));
    ASSERT_TRUE(result.accepted);
    EXPECT_EQ(oms.position(), -2);
}

TEST(OMSExternalTest, ExternalSellPartiallyFillsOurRestingBuy) {
    OrderManagementSystem oms{test_limits()};
    ASSERT_TRUE(oms.submit(make_order(1, Side::Buy, 5, 100)).accepted);

    const auto trades =
        oms.add_external_liquidity(make_order(100, Side::Sell, 2, 100));

    ASSERT_EQ(trades.size(), 1U);
    EXPECT_EQ(oms.position(), 2);
    EXPECT_EQ(oms.get_order(OrderId{1}).status(),
              OrderStatus::PartiallyFilled);
    EXPECT_EQ(oms.get_order(OrderId{1}).remaining_quantity(), 3U);
}

TEST(OMSExternalTest, ExternalToExternalTradeDoesNotChangeOurPosition) {
    OrderManagementSystem oms{test_limits()};
    (void)oms.add_external_liquidity(make_order(100, Side::Buy, 2, 100));

    const auto trades =
        oms.add_external_liquidity(make_order(101, Side::Sell, 2, 100));

    ASSERT_EQ(trades.size(), 1U);
    EXPECT_EQ(oms.position(), 0);
    EXPECT_EQ(oms.active_order_count(), 0U);
}


TEST(OMSExternalTest, PendingOppositeOrdersCannotHideLongExposure) {
    OrderManagementSystem oms{test_limits()};
    ASSERT_TRUE(oms.submit(make_order(1, Side::Buy, 8, 99)).accepted);
    ASSERT_TRUE(oms.submit(make_order(2, Side::Sell, 8, 101)).accepted);

    const auto result = oms.submit(make_order(3, Side::Buy, 5, 98));
    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.reason.find("Worst-case"), std::string::npos);
}

TEST(OMSExternalTest, PendingOppositeOrdersCannotHideShortExposure) {
    OrderManagementSystem oms{test_limits()};
    ASSERT_TRUE(oms.submit(make_order(1, Side::Sell, 8, 101)).accepted);
    ASSERT_TRUE(oms.submit(make_order(2, Side::Buy, 8, 99)).accepted);

    const auto result = oms.submit(make_order(3, Side::Sell, 5, 102));
    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.reason.find("Worst-case"), std::string::npos);
}

}  // namespace
}  // namespace apexhft
