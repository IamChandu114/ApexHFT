#include "apexhft/oms.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace apexhft {
namespace {

Order make_order(std::uint64_t id, Side side, std::uint64_t quantity,
                 std::int64_t price = 100) {
    return Order{OrderId{id}, side, OrderType::Limit,
                 Quantity{quantity}, PriceTicks{price}};
}

RiskLimits limits() {
    return RiskLimits{10, 10, 1, 1000};
}

TEST(OMSAuditTest, RecordsAcceptedOrder) {
    OrderManagementSystem oms{limits()};
    ASSERT_TRUE(oms.submit(make_order(1, Side::Buy, 3)).accepted);

    const auto& events = oms.event_log().events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].sequence, 1U);
    EXPECT_EQ(events[0].type, EventType::OrderAccepted);
    EXPECT_EQ(events[0].details, "order_id=1");
}

TEST(OMSAuditTest, RecordsRejectedOrderAndReason) {
    OrderManagementSystem oms{limits()};
    const auto result = oms.submit(make_order(2, Side::Buy, 11));

    ASSERT_FALSE(result.accepted);
    const auto& events = oms.event_log().events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].type, EventType::OrderRejected);
    EXPECT_NE(events[0].details.find("order_id=2"), std::string::npos);
    EXPECT_NE(events[0].details.find(result.reason), std::string::npos);
}

TEST(OMSAuditTest, RecordsSuccessfulCancellation) {
    OrderManagementSystem oms{limits()};
    ASSERT_TRUE(oms.submit(make_order(1, Side::Buy, 3)).accepted);
    oms.cancel(OrderId{1});

    const auto& events = oms.event_log().events();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].sequence, 1U);
    EXPECT_EQ(events[1].sequence, 2U);
    EXPECT_EQ(events[1].type, EventType::OrderCancelled);
    EXPECT_EQ(events[1].details, "order_id=1");
}

TEST(OMSAuditTest, RecordsAcceptanceBeforeExecution) {
    OrderManagementSystem oms{limits()};
    (void)oms.add_external_liquidity(make_order(100, Side::Sell, 5));

    const auto result = oms.submit(make_order(1, Side::Buy, 2));
    ASSERT_TRUE(result.accepted);

    const auto& events = oms.event_log().events();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].type, EventType::OrderAccepted);
    EXPECT_EQ(events[1].type, EventType::TradeExecuted);
    EXPECT_EQ(events[0].sequence, 1U);
    EXPECT_EQ(events[1].sequence, 2U);
    EXPECT_NE(events[1].details.find("price_ticks=100"), std::string::npos);
    EXPECT_NE(events[1].details.find("quantity=2"), std::string::npos);
}

TEST(OMSAuditTest, RecordsExecutionTriggeredByExternalLiquidity) {
    OrderManagementSystem oms{limits()};
    ASSERT_TRUE(oms.submit(make_order(1, Side::Buy, 4)).accepted);

    (void)oms.add_external_liquidity(make_order(100, Side::Sell, 2));

    const auto& events = oms.event_log().events();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].type, EventType::OrderAccepted);
    EXPECT_EQ(events[1].type, EventType::TradeExecuted);
    EXPECT_EQ(oms.position(), 2);
}

TEST(OMSAuditTest, RecordsRejectionOfSelfCross) {
    OrderManagementSystem oms{limits()};
    ASSERT_TRUE(oms.submit(make_order(1, Side::Buy, 3, 100)).accepted);

    const auto result = oms.submit(make_order(2, Side::Sell, 3, 100));
    ASSERT_FALSE(result.accepted);

    const auto& events = oms.event_log().events();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[1].type, EventType::OrderRejected);
    EXPECT_NE(events[1].details.find("Self-cross prevented"),
              std::string::npos);
}

}  // namespace
}  // namespace apexhft
