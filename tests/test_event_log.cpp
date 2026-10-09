#include "apexhft/event_log.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace apexhft {
namespace {

TEST(EventLogTest, AssignsContiguousSequenceNumbers) {
    EventLog log;
    log.append(EventType::OrderAccepted, "order=1");
    log.append(EventType::TradeExecuted, "trade=1");

    ASSERT_EQ(log.events().size(), 2U);
    EXPECT_EQ(log.events()[0].sequence, 1U);
    EXPECT_EQ(log.events()[1].sequence, 2U);
    EXPECT_EQ(log.size(), 2U);
}

TEST(EventLogTest, PreservesEventTypesAndDetails) {
    EventLog log;
    log.append(EventType::OrderRejected, "risk limit exceeded");

    ASSERT_EQ(log.events().size(), 1U);
    EXPECT_EQ(log.events()[0].type, EventType::OrderRejected);
    EXPECT_EQ(log.events()[0].details, "risk limit exceeded");
}

TEST(EventLogTest, KeepsEarlierEventsWhenAppendingMore) {
    EventLog log;
    log.append(EventType::OrderAccepted, "order=10");
    log.append(EventType::OrderCancelled, "order=10");

    ASSERT_EQ(log.events().size(), 2U);
    EXPECT_EQ(log.events()[0].details, "order=10");
    EXPECT_EQ(log.events()[1].sequence, 2U);
}

}  // namespace
}  // namespace apexhft
