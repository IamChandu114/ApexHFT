#include "apexhft/matching_engine.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <random>
#include <optional>

using namespace apexhft;

namespace {
Order limit_order(std::uint64_t id, Side side,
                  std::uint64_t qty, std::int64_t price) {
    return Order{OrderId{id}, side, OrderType::Limit,
                 Quantity{qty}, PriceTicks{price}};
}
}

TEST(MatchingEngineTest, KeepsNonCrossingOrdersOnBook) {
    MatchingEngine engine;
    EXPECT_TRUE(engine.submit(limit_order(1, Side::Buy, 5, 99)).empty());
    EXPECT_TRUE(engine.submit(limit_order(2, Side::Sell, 4, 101)).empty());

    ASSERT_TRUE(engine.best_bid().has_value());
    ASSERT_TRUE(engine.best_ask().has_value());
    EXPECT_EQ(engine.best_bid()->value(), 99);
    EXPECT_EQ(engine.best_ask()->value(), 101);
    EXPECT_EQ(engine.active_order_count(), 2);
}

TEST(MatchingEngineTest, MatchesAtRestingAskPrice) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Sell, 5, 100));
    const auto trades = engine.submit(limit_order(2, Side::Buy, 3, 105));

    ASSERT_EQ(trades.size(), 1U);
    EXPECT_EQ(trades[0].price.value(), 100);
    EXPECT_EQ(trades[0].quantity.value(), 3);
    EXPECT_EQ(trades[0].buy_order_id.value(), 2);
    EXPECT_EQ(trades[0].sell_order_id.value(), 1);
    EXPECT_EQ(engine.get_order(OrderId{1}).remaining_quantity(), 2);
    EXPECT_EQ(engine.get_order(OrderId{1}).status(), OrderStatus::PartiallyFilled);
}

TEST(MatchingEngineTest, MatchesAtRestingBidPrice) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Buy, 4, 105));
    const auto trades = engine.submit(limit_order(2, Side::Sell, 2, 100));

    ASSERT_EQ(trades.size(), 1U);
    EXPECT_EQ(trades[0].price.value(), 105);
    EXPECT_EQ(trades[0].quantity.value(), 2);
    EXPECT_EQ(engine.get_order(OrderId{2}).status(), OrderStatus::Filled);
}

TEST(MatchingEngineTest, RespectsPriceTimePriority) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Sell, 2, 100));
    (void)engine.submit(limit_order(2, Side::Sell, 2, 100));

    const auto trades = engine.submit(limit_order(3, Side::Buy, 3, 100));

    ASSERT_EQ(trades.size(), 2U);
    EXPECT_EQ(trades[0].sell_order_id.value(), 1);
    EXPECT_EQ(trades[0].quantity.value(), 2);
    EXPECT_EQ(trades[1].sell_order_id.value(), 2);
    EXPECT_EQ(trades[1].quantity.value(), 1);
    EXPECT_EQ(engine.get_order(OrderId{1}).status(), OrderStatus::Filled);
    EXPECT_EQ(engine.get_order(OrderId{2}).remaining_quantity(), 1);
}

TEST(MatchingEngineTest, PreservesIncomingRemainder) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Sell, 2, 100));
    const auto trades = engine.submit(limit_order(2, Side::Buy, 5, 101));

    ASSERT_EQ(trades.size(), 1U);
    EXPECT_EQ(engine.get_order(OrderId{2}).filled_quantity(), 2);
    EXPECT_EQ(engine.get_order(OrderId{2}).remaining_quantity(), 3);
    EXPECT_EQ(engine.get_order(OrderId{2}).status(), OrderStatus::PartiallyFilled);
    ASSERT_TRUE(engine.best_bid().has_value());
    EXPECT_EQ(engine.best_bid()->value(), 101);
}

TEST(MatchingEngineTest, CancelsOrderAndUpdatesBestPrice) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Buy, 2, 99));
    (void)engine.submit(limit_order(2, Side::Buy, 2, 98));

    engine.cancel(OrderId{1});

    ASSERT_TRUE(engine.best_bid().has_value());
    EXPECT_EQ(engine.best_bid()->value(), 98);
    EXPECT_EQ(engine.get_order(OrderId{1}).status(), OrderStatus::Cancelled);
    EXPECT_EQ(engine.active_order_count(), 1);
}

TEST(MatchingEngineTest, RejectsDuplicateOrderIds) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Buy, 2, 99));
    EXPECT_THROW(engine.submit(limit_order(1, Side::Sell, 2, 100)),
                 std::invalid_argument);
}

TEST(MatchingEngineTest, RejectsMarketOrdersForNow) {
    MatchingEngine engine;
    Order market{OrderId{1}, Side::Buy, OrderType::Market, Quantity{2}};
    EXPECT_THROW(engine.submit(std::move(market)), std::invalid_argument);
}

TEST(MatchingEngineTest, RejectsCancellationOfUnknownOrInactiveOrder) {
    MatchingEngine engine;
    EXPECT_THROW(engine.cancel(OrderId{99}), std::out_of_range);

    (void)engine.submit(limit_order(1, Side::Buy, 2, 99));
    engine.cancel(OrderId{1});
    EXPECT_THROW(engine.cancel(OrderId{1}), std::logic_error);
}

TEST(MatchingEngineTest, DoesNotCrossNonMarketablePrices) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Sell, 2, 101));
    const auto trades = engine.submit(limit_order(2, Side::Buy, 2, 100));

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(engine.get_order(OrderId{2}).status(), OrderStatus::New);
}

TEST(MatchingEngineTest, PrioritizesBestPriceBeforeTime) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Sell, 2, 102));
    (void)engine.submit(limit_order(2, Side::Sell, 1, 100));

    const auto trades = engine.submit(limit_order(3, Side::Buy, 3, 105));

    ASSERT_EQ(trades.size(), 2U);
    EXPECT_EQ(trades[0].sell_order_id.value(), 2);
    EXPECT_EQ(trades[0].price.value(), 100);
    EXPECT_EQ(trades[1].sell_order_id.value(), 1);
    EXPECT_EQ(trades[1].price.value(), 102);
}

TEST(MatchingEngineTest, AssignsUniqueIncreasingTradeIds) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Sell, 1, 100));
    (void)engine.submit(limit_order(2, Side::Sell, 1, 101));

    const auto first = engine.submit(limit_order(3, Side::Buy, 1, 100));
    const auto second = engine.submit(limit_order(4, Side::Buy, 1, 101));

    ASSERT_EQ(first.size(), 1U);
    ASSERT_EQ(second.size(), 1U);
    EXPECT_LT(first[0].id.value(), second[0].id.value());
}

TEST(MatchingEngineTest, CancelsPartiallyFilledRestingOrder) {
    MatchingEngine engine;
    (void)engine.submit(limit_order(1, Side::Sell, 5, 100));
    (void)engine.submit(limit_order(2, Side::Buy, 2, 100));

    ASSERT_TRUE(engine.best_ask().has_value());
    EXPECT_EQ(engine.get_order(OrderId{1}).remaining_quantity(), 3);

    engine.cancel(OrderId{1});

    EXPECT_FALSE(engine.best_ask().has_value());
    EXPECT_EQ(engine.get_order(OrderId{1}).status(), OrderStatus::Cancelled);
    EXPECT_EQ(engine.get_order(OrderId{1}).filled_quantity(), 2);
}

TEST(MatchingEngineTest, SeededStressPreservesBookInvariants) {
    MatchingEngine engine;
    std::mt19937_64 rng{0xA9E5F17ULL};
    std::uniform_int_distribution<int> action_dist{0, 99};
    std::uniform_int_distribution<int> side_dist{0, 1};
    std::uniform_int_distribution<int> quantity_dist{1, 10};
    std::uniform_int_distribution<int> price_dist{90, 110};

    std::vector<std::uint64_t> ids;
    std::uint64_t next_id = 1;

    for (int step = 0; step < 1000; ++step) {
        std::vector<std::uint64_t> active_ids;
        for (const auto id : ids) {
            const auto& order = engine.get_order(OrderId{id});
            if (order.status() == OrderStatus::New ||
                order.status() == OrderStatus::PartiallyFilled) {
                active_ids.push_back(id);
            }
        }

        if (!active_ids.empty() && action_dist(rng) < 20) {
            std::uniform_int_distribution<std::size_t> pick{
                0, active_ids.size() - 1};
            engine.cancel(OrderId{active_ids[pick(rng)]});
        } else {
            const auto side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
            const auto quantity = static_cast<std::uint64_t>(quantity_dist(rng));
            const auto price = static_cast<std::int64_t>(price_dist(rng));
            const auto id = next_id++;

            const auto trades = engine.submit(
                limit_order(id, side, quantity, price));

            for (const auto& trade : trades) {
                EXPECT_GT(trade.quantity.value(), 0U);
                EXPECT_GT(trade.price.value(), 0);
                EXPECT_TRUE(trade.buy_order_id.value() == id ||
                            trade.sell_order_id.value() == id);

                const auto other_id =
                    trade.buy_order_id.value() == id
                        ? trade.sell_order_id.value()
                        : trade.buy_order_id.value();

                EXPECT_EQ(
                    engine.get_order(OrderId{other_id})
                        .limit_price()->value(),
                    trade.price.value());
            }

            ids.push_back(id);
        }

        std::optional<std::int64_t> expected_bid;
        std::optional<std::int64_t> expected_ask;
        std::size_t expected_active = 0;

        for (const auto id : ids) {
            const auto& order = engine.get_order(OrderId{id});
            EXPECT_LE(order.filled_quantity(), order.quantity().value());

            if (order.status() != OrderStatus::New &&
                order.status() != OrderStatus::PartiallyFilled) {
                continue;
            }

            ++expected_active;
            const auto price = order.limit_price()->value();

            if (order.side() == Side::Buy) {
                if (!expected_bid || price > *expected_bid)
                    expected_bid = price;
            } else {
                if (!expected_ask || price < *expected_ask)
                    expected_ask = price;
            }
        }

        EXPECT_EQ(engine.active_order_count(), expected_active);
        EXPECT_EQ(engine.best_bid().has_value(), expected_bid.has_value());
        EXPECT_EQ(engine.best_ask().has_value(), expected_ask.has_value());

        if (expected_bid && engine.best_bid()) {
            EXPECT_EQ(engine.best_bid()->value(), *expected_bid);
        }
        if (expected_ask && engine.best_ask()) {
            EXPECT_EQ(engine.best_ask()->value(), *expected_ask);
        }

        if (expected_bid && expected_ask) {
            EXPECT_LT(*expected_bid, *expected_ask);
        }
    }
}

TEST(MatchingEngineTest, MatchesIndependentReferenceModel) {
    struct RefOrder {
        std::uint64_t id;
        Side side;
        std::uint64_t quantity;
        std::int64_t price;
        std::uint64_t filled{0};
    };

    MatchingEngine engine;
    std::vector<RefOrder> ref;
    std::mt19937_64 rng{0xC0FFEE123ULL};
    std::uniform_int_distribution<int> side_dist{0, 1};
    std::uniform_int_distribution<int> qty_dist{1, 10};
    std::uniform_int_distribution<int> price_dist{90, 110};
    std::uint64_t next_trade_id = 1;

    for (std::uint64_t id = 1; id <= 500; ++id) {
        const Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
        const auto qty = static_cast<std::uint64_t>(qty_dist(rng));
        const auto price = static_cast<std::int64_t>(price_dist(rng));

        ref.push_back(RefOrder{id, side, qty, price, 0});
        auto& incoming = ref.back();

        struct ExpectedTrade {
            std::uint64_t buy_id;
            std::uint64_t sell_id;
            std::int64_t price;
            std::uint64_t quantity;
        };
        std::vector<ExpectedTrade> expected;

        while (incoming.filled < incoming.quantity) {
            std::optional<std::size_t> best;

            for (std::size_t i = 0; i + 1 < ref.size(); ++i) {
                const auto& candidate = ref[i];
                if (candidate.filled == candidate.quantity ||
                    candidate.side == side) {
                    continue;
                }

                const bool crosses = side == Side::Buy
                    ? candidate.price <= price
                    : candidate.price >= price;
                if (!crosses) {
                    continue;
                }

                if (!best) {
                    best = i;
                    continue;
                }

                const auto& current = ref[*best];
                const bool better_price = side == Side::Buy
                    ? candidate.price < current.price
                    : candidate.price > current.price;

                if (better_price) {
                    best = i;
                }
            }

            if (!best) {
                break;
            }

            auto& resting = ref[*best];
            const auto remaining_in = incoming.quantity - incoming.filled;
            const auto remaining_rest = resting.quantity - resting.filled;
            const auto fill = std::min(remaining_in, remaining_rest);

            expected.push_back(ExpectedTrade{
                side == Side::Buy ? id : resting.id,
                side == Side::Sell ? id : resting.id,
                resting.price,
                fill
            });

            incoming.filled += fill;
            resting.filled += fill;
        }

        const auto actual = engine.submit(limit_order(id, side, qty, price));
        ASSERT_EQ(actual.size(), expected.size()) << "order ID " << id;

        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(actual[i].buy_order_id.value(), expected[i].buy_id);
            EXPECT_EQ(actual[i].sell_order_id.value(), expected[i].sell_id);
            EXPECT_EQ(actual[i].price.value(), expected[i].price);
            EXPECT_EQ(actual[i].quantity.value(), expected[i].quantity);
            EXPECT_EQ(actual[i].id.value(), next_trade_id++);
        }

        std::optional<std::int64_t> expected_bid;
        std::optional<std::int64_t> expected_ask;
        std::size_t active = 0;

        for (const auto& order : ref) {
            const auto& actual_order = engine.get_order(OrderId{order.id});
            EXPECT_EQ(actual_order.filled_quantity(), order.filled);
            EXPECT_EQ(actual_order.remaining_quantity(),
                      order.quantity - order.filled);

            const auto expected_status = order.filled == order.quantity
                ? OrderStatus::Filled
                : (order.filled > 0
                    ? OrderStatus::PartiallyFilled : OrderStatus::New);
            EXPECT_EQ(actual_order.status(), expected_status);

            if (order.filled == order.quantity) {
                continue;
            }

            ++active;
            if (order.side == Side::Buy) {
                if (!expected_bid || order.price > *expected_bid) {
                    expected_bid = order.price;
                }
            } else {
                if (!expected_ask || order.price < *expected_ask) {
                    expected_ask = order.price;
                }
            }
        }

        EXPECT_EQ(engine.active_order_count(), active);
        EXPECT_EQ(engine.best_bid().has_value(), expected_bid.has_value());
        EXPECT_EQ(engine.best_ask().has_value(), expected_ask.has_value());

        if (expected_bid && engine.best_bid()) {
            EXPECT_EQ(engine.best_bid()->value(), *expected_bid);
        }
        if (expected_ask && engine.best_ask()) {
            EXPECT_EQ(engine.best_ask()->value(), *expected_ask);
        }
    }
}
