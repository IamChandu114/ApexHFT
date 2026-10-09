#include "apexhft/oms.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace apexhft {
namespace {

Order make_order(std::uint64_t id, Side side, std::uint64_t quantity,
                 std::int64_t price = 100,
                 OrderType type = OrderType::Limit) {
    return Order{
        OrderId{id}, side, type, Quantity{quantity},
        type == OrderType::Limit
            ? std::optional<PriceTicks>{PriceTicks{price}}
            : std::nullopt
    };
}

RiskLimits limits() {
    return RiskLimits{10, 10, 1, 1000};
}

TEST(OMSTest, AcceptsValidRestingOrder) {
    OrderManagementSystem oms{limits()};
    const auto result = oms.submit(make_order(1, Side::Buy, 5));
    EXPECT_TRUE(result.accepted);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(oms.active_order_count(), 1U);
}

TEST(OMSTest, RejectsOrdersThatExceedPendingExposureLimit) {
    OrderManagementSystem oms{limits()};
    EXPECT_TRUE(oms.submit(make_order(1, Side::Buy, 6)).accepted);

    const auto result = oms.submit(make_order(2, Side::Buy, 5, 99));
    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(oms.active_order_count(), 1U);
}

TEST(OMSTest, CancellationReleasesPendingExposure) {
    OrderManagementSystem oms{limits()};
    EXPECT_TRUE(oms.submit(make_order(1, Side::Buy, 6)).accepted);
    oms.cancel(OrderId{1});

    const auto result = oms.submit(make_order(2, Side::Buy, 6, 99));
    EXPECT_TRUE(result.accepted);
}

TEST(OMSTest, RejectsCrossingOrdersFromSameOms) {
    OrderManagementSystem oms{limits()};
    EXPECT_TRUE(oms.submit(make_order(1, Side::Buy, 5, 100)).accepted);

    const auto result = oms.submit(make_order(2, Side::Sell, 5, 100));
    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.reason.find("Self-cross"), std::string::npos);
    EXPECT_EQ(oms.active_order_count(), 1U);
}

TEST(OMSTest, RejectsUnsupportedMarketOrderWithoutThrowing) {
    OrderManagementSystem oms{limits()};
    const auto result =
        oms.submit(make_order(1, Side::Buy, 1, 100, OrderType::Market));

    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.reason.find("limit orders only"), std::string::npos);
    EXPECT_EQ(oms.active_order_count(), 0U);
}

TEST(OMSTest, NonCrossingOppositeOrdersCanRest) {
    OrderManagementSystem oms{limits()};
    EXPECT_TRUE(oms.submit(make_order(1, Side::Buy, 3, 99)).accepted);

    const auto result = oms.submit(make_order(2, Side::Sell, 3, 101));
    EXPECT_TRUE(result.accepted);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(oms.active_order_count(), 2U);
}

}  // namespace
}  // namespace apexhft
