#include "apexhft/replay.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using namespace apexhft;

Order make_order(std::uint64_t id, Side side,
                 std::uint64_t quantity, std::int64_t price) {
    return Order{OrderId{id}, side, OrderType::Limit,
                 Quantity{quantity}, PriceTicks{price}};
}

std::vector<SequencedEvent> sample_events() {
    return {
        {1, SubmitEvent{make_order(1, Side::Sell, 5, 100)}},
        {2, SubmitEvent{make_order(2, Side::Buy, 3, 100)}},
        {3, SubmitEvent{make_order(3, Side::Buy, 4, 99)}}
    };
}

TEST(MarketReplayTest, RepeatedReplayProducesIdenticalTrades) {
    const MarketReplay replay;
    const auto events = sample_events();

    const auto first = replay.run(events);
    const auto second = replay.run(events);

    ASSERT_EQ(first.trades.size(), second.trades.size());
    EXPECT_EQ(first.events_processed, 3U);
    EXPECT_EQ(second.events_processed, 3U);

    for (std::size_t i = 0; i < first.trades.size(); ++i) {
        EXPECT_EQ(first.trades[i].id.value(), second.trades[i].id.value());
        EXPECT_EQ(first.trades[i].buy_order_id.value(),
                  second.trades[i].buy_order_id.value());
        EXPECT_EQ(first.trades[i].sell_order_id.value(),
                  second.trades[i].sell_order_id.value());
        EXPECT_EQ(first.trades[i].price.value(),
                  second.trades[i].price.value());
        EXPECT_EQ(first.trades[i].quantity.value(),
                  second.trades[i].quantity.value());
    }
}

TEST(MarketReplayTest, ProducesExpectedTrade) {
    const MarketReplay replay;
    const auto result = replay.run(sample_events());

    ASSERT_EQ(result.trades.size(), 1U);
    EXPECT_EQ(result.trades[0].buy_order_id.value(), 2U);
    EXPECT_EQ(result.trades[0].sell_order_id.value(), 1U);
    EXPECT_EQ(result.trades[0].price.value(), 100);
    EXPECT_EQ(result.trades[0].quantity.value(), 3U);
}

TEST(MarketReplayTest, AppliesCancellationEvents) {
    const MarketReplay replay;
    const std::vector<SequencedEvent> events{
        {1, SubmitEvent{make_order(1, Side::Buy, 5, 99)}},
        {2, CancelEvent{OrderId{1}}}
    };

    const auto result = replay.run(events);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.events_processed, 2U);
}

TEST(MarketReplayTest, RejectsNonContiguousSequenceNumbers) {
    const MarketReplay replay;
    const std::vector<SequencedEvent> events{
        {1, SubmitEvent{make_order(1, Side::Sell, 5, 100)}},
        {3, SubmitEvent{make_order(2, Side::Buy, 5, 100)}}
    };

    EXPECT_THROW(replay.run(events), std::invalid_argument);
}

}  // namespace
