#include "apexhft/oms.hpp"
#include "apexhft/oms_replay.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

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

OmsReplayCommand submit_owned(std::uint64_t seq, Order order) {
    return OmsReplayCommand{
        seq, OmsReplayOp::SubmitOwned, std::move(order), std::nullopt};
}

OmsReplayCommand cancel_owned(std::uint64_t seq, std::uint64_t id) {
    return OmsReplayCommand{
        seq, OmsReplayOp::CancelOwned, std::nullopt, OrderId{id}};
}

OmsReplayCommand add_external(std::uint64_t seq, Order order) {
    return OmsReplayCommand{
        seq, OmsReplayOp::AddExternalLiquidity, std::move(order), std::nullopt};
}

void expect_same_trade(const Trade& left, const Trade& right) {
    EXPECT_EQ(left.id.value(), right.id.value());
    EXPECT_EQ(left.buy_order_id.value(), right.buy_order_id.value());
    EXPECT_EQ(left.sell_order_id.value(), right.sell_order_id.value());
    EXPECT_EQ(left.price.value(), right.price.value());
    EXPECT_EQ(left.quantity.value(), right.quantity.value());
}

void expect_same_audit(const AuditEvent& left, const AuditEvent& right) {
    EXPECT_EQ(left.sequence, right.sequence);
    EXPECT_EQ(left.type, right.type);
    EXPECT_EQ(left.details, right.details);
}

void expect_same_result(const OmsReplayResult& left,
                        const OmsReplayResult& right) {
    EXPECT_EQ(left.position, right.position);
    EXPECT_EQ(left.active_owned_orders, right.active_owned_orders);
    EXPECT_EQ(left.commands_processed, right.commands_processed);
    ASSERT_EQ(left.trades.size(), right.trades.size());
    for (std::size_t i = 0; i < left.trades.size(); ++i) {
        expect_same_trade(left.trades[i], right.trades[i]);
    }
    ASSERT_EQ(left.audit_events.size(), right.audit_events.size());
    for (std::size_t i = 0; i < left.audit_events.size(); ++i) {
        expect_same_audit(left.audit_events[i], right.audit_events[i]);
    }
    ASSERT_EQ(left.commands.size(), right.commands.size());
    for (std::size_t i = 0; i < left.commands.size(); ++i) {
        EXPECT_EQ(left.commands[i].sequence, right.commands[i].sequence);
        EXPECT_EQ(left.commands[i].op, right.commands[i].op);
        EXPECT_EQ(left.commands[i].accepted, right.commands[i].accepted);
        EXPECT_EQ(left.commands[i].reason, right.commands[i].reason);
        ASSERT_EQ(left.commands[i].trades.size(),
                  right.commands[i].trades.size());
        for (std::size_t t = 0; t < left.commands[i].trades.size(); ++t) {
            expect_same_trade(left.commands[i].trades[t],
                              right.commands[i].trades[t]);
        }
    }
    ASSERT_EQ(left.remaining_owned_orders.size(),
              right.remaining_owned_orders.size());
    for (std::size_t i = 0; i < left.remaining_owned_orders.size(); ++i) {
        EXPECT_EQ(left.remaining_owned_orders[i].id.value(),
                  right.remaining_owned_orders[i].id.value());
        EXPECT_EQ(left.remaining_owned_orders[i].side,
                  right.remaining_owned_orders[i].side);
        EXPECT_EQ(left.remaining_owned_orders[i].remaining_quantity,
                  right.remaining_owned_orders[i].remaining_quantity);
        EXPECT_EQ(left.remaining_owned_orders[i].limit_price_ticks,
                  right.remaining_owned_orders[i].limit_price_ticks);
        EXPECT_EQ(left.remaining_owned_orders[i].status,
                  right.remaining_owned_orders[i].status);
    }
}

OmsReplayResult apply_directly(const RiskLimits& risk,
                               const std::vector<OmsReplayCommand>& commands) {
    OrderManagementSystem oms{risk};
    OmsReplayResult result;
    std::uint64_t expected = 1;

    for (const auto& command : commands) {
        EXPECT_EQ(command.sequence, expected);
        OmsCommandRecord record;
        record.sequence = command.sequence;
        record.op = command.op;

        switch (command.op) {
        case OmsReplayOp::SubmitOwned: {
            auto submission = oms.submit(*command.order);
            record.accepted = submission.accepted;
            record.reason = std::move(submission.reason);
            record.trades = std::move(submission.trades);
            break;
        }
        case OmsReplayOp::CancelOwned:
            oms.cancel(*command.cancel_id);
            break;
        case OmsReplayOp::AddExternalLiquidity:
            record.trades = oms.add_external_liquidity(*command.order);
            break;
        }

        result.trades.insert(result.trades.end(),
                             record.trades.begin(), record.trades.end());
        result.commands.push_back(std::move(record));
        ++expected;
        ++result.commands_processed;
    }

    result.position = oms.position();
    result.active_owned_orders = oms.active_order_count();
    result.audit_events = oms.event_log().events();
    for (const auto id : oms.owned_order_ids()) {
        const auto& order = oms.get_order(id);
        if (order.status() == OrderStatus::New ||
            order.status() == OrderStatus::PartiallyFilled) {
            result.remaining_owned_orders.push_back(snapshot_order(order));
        }
    }
    return result;
}

std::vector<OmsReplayCommand> mixed_sequence() {
    return {
        add_external(1, make_order(100, Side::Sell, 5, 100)),
        submit_owned(2, make_order(1, Side::Buy, 3, 100)),
        submit_owned(3, make_order(2, Side::Buy, 4, 99)),
        add_external(4, make_order(101, Side::Sell, 2, 99)),
        cancel_owned(5, 2),
        submit_owned(6, make_order(3, Side::Sell, 1, 101))
    };
}

TEST(OmsReplayTest, RepeatedReplayMatchesExactly) {
    const OmsReplay replay{limits()};
    const auto commands = mixed_sequence();

    const auto first = replay.run(commands);
    const auto second = replay.run(commands);
    expect_same_result(first, second);
    EXPECT_EQ(first.commands_processed, 6U);
}

TEST(OmsReplayTest, ReconstructsPositionAndTradeSequence) {
    const auto result = OmsReplay{limits()}.run(mixed_sequence());

    ASSERT_EQ(result.trades.size(), 2U);
    EXPECT_EQ(result.trades[0].id.value(), 1U);
    EXPECT_EQ(result.trades[0].buy_order_id.value(), 1U);
    EXPECT_EQ(result.trades[0].sell_order_id.value(), 100U);
    EXPECT_EQ(result.trades[0].price.value(), 100);
    EXPECT_EQ(result.trades[0].quantity.value(), 3U);

    EXPECT_EQ(result.trades[1].id.value(), 2U);
    EXPECT_EQ(result.trades[1].buy_order_id.value(), 2U);
    EXPECT_EQ(result.trades[1].sell_order_id.value(), 101U);
    EXPECT_EQ(result.trades[1].price.value(), 99);
    EXPECT_EQ(result.trades[1].quantity.value(), 2U);

    EXPECT_EQ(result.position, 5);
    EXPECT_EQ(result.active_owned_orders, 1U);
    ASSERT_EQ(result.remaining_owned_orders.size(), 1U);
    EXPECT_EQ(result.remaining_owned_orders[0].id.value(), 3U);
    EXPECT_EQ(result.remaining_owned_orders[0].remaining_quantity, 1U);
}

TEST(OmsReplayTest, OwnedSubmissionRestsWithoutFill) {
    const auto result = OmsReplay{limits()}.run({
        submit_owned(1, make_order(1, Side::Buy, 5, 99))
    });

    EXPECT_TRUE(result.trades.empty());
    ASSERT_EQ(result.commands.size(), 1U);
    EXPECT_TRUE(result.commands[0].accepted);
    EXPECT_EQ(result.position, 0);
    ASSERT_EQ(result.remaining_owned_orders.size(), 1U);
    EXPECT_EQ(result.remaining_owned_orders[0].id.value(), 1U);
}

TEST(OmsReplayTest, ExternalLiquidityFillsOwnedOrder) {
    const auto result = OmsReplay{limits()}.run({
        add_external(1, make_order(100, Side::Sell, 5, 100)),
        submit_owned(2, make_order(1, Side::Buy, 3, 100))
    });

    ASSERT_EQ(result.trades.size(), 1U);
    EXPECT_EQ(result.position, 3);
    EXPECT_TRUE(result.remaining_owned_orders.empty());
}

TEST(OmsReplayTest, PartialAndMultipleFills) {
    const auto result = OmsReplay{limits()}.run({
        submit_owned(1, make_order(1, Side::Buy, 5, 100)),
        add_external(2, make_order(100, Side::Sell, 2, 100)),
        add_external(3, make_order(101, Side::Sell, 2, 100))
    });

    ASSERT_EQ(result.trades.size(), 2U);
    EXPECT_EQ(result.trades[0].quantity.value(), 2U);
    EXPECT_EQ(result.trades[1].quantity.value(), 2U);
    EXPECT_EQ(result.position, 4);
    ASSERT_EQ(result.remaining_owned_orders.size(), 1U);
    EXPECT_EQ(result.remaining_owned_orders[0].remaining_quantity, 1U);
    EXPECT_EQ(result.remaining_owned_orders[0].status,
              OrderStatus::PartiallyFilled);
}

TEST(OmsReplayTest, CancellationRemovesRestingOwnedOrder) {
    const auto result = OmsReplay{limits()}.run({
        submit_owned(1, make_order(1, Side::Buy, 5, 99)),
        cancel_owned(2, 1),
        submit_owned(3, make_order(2, Side::Buy, 6, 99))
    });

    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.active_owned_orders, 1U);
    ASSERT_EQ(result.remaining_owned_orders.size(), 1U);
    EXPECT_EQ(result.remaining_owned_orders[0].id.value(), 2U);
    ASSERT_GE(result.audit_events.size(), 2U);
    EXPECT_EQ(result.audit_events[1].type, EventType::OrderCancelled);
}

TEST(OmsReplayTest, RiskRejectionDoesNotCorruptState) {
    const auto result = OmsReplay{limits()}.run({
        submit_owned(1, make_order(1, Side::Buy, 6, 100)),
        submit_owned(2, make_order(2, Side::Buy, 5, 99)),
        submit_owned(3, make_order(3, Side::Sell, 1, 101))
    });

    ASSERT_EQ(result.commands.size(), 3U);
    EXPECT_TRUE(result.commands[0].accepted);
    EXPECT_FALSE(result.commands[1].accepted);
    EXPECT_NE(result.commands[1].reason.find("Worst-case"), std::string::npos);
    EXPECT_TRUE(result.commands[2].accepted);
    EXPECT_EQ(result.position, 0);
    EXPECT_EQ(result.active_owned_orders, 2U);
    EXPECT_TRUE(result.trades.empty());
}

TEST(OmsReplayTest, SelfCrossRejectionIsDeterministic) {
    const auto commands = std::vector<OmsReplayCommand>{
        submit_owned(1, make_order(1, Side::Buy, 3, 100)),
        submit_owned(2, make_order(2, Side::Sell, 3, 100))
    };

    const auto first = OmsReplay{limits()}.run(commands);
    const auto second = OmsReplay{limits()}.run(commands);
    expect_same_result(first, second);

    EXPECT_TRUE(first.commands[0].accepted);
    EXPECT_FALSE(first.commands[1].accepted);
    EXPECT_NE(first.commands[1].reason.find("Self-cross"), std::string::npos);
    EXPECT_EQ(first.active_owned_orders, 1U);
    EXPECT_TRUE(first.trades.empty());
}

TEST(OmsReplayTest, DuplicateOwnedIdThrowsLikeDirectOms) {
    const std::vector<OmsReplayCommand> commands{
        submit_owned(1, make_order(1, Side::Buy, 1, 99)),
        submit_owned(2, make_order(1, Side::Buy, 1, 98))
    };

    OrderManagementSystem oms{limits()};
    ASSERT_TRUE(oms.submit(make_order(1, Side::Buy, 1, 99)).accepted);
    EXPECT_THROW(oms.submit(make_order(1, Side::Buy, 1, 98)),
                 std::invalid_argument);
    EXPECT_THROW(OmsReplay{limits()}.run(commands), std::invalid_argument);
}

TEST(OmsReplayTest, RejectsInvalidAndDiscontinuousSequences) {
    const OmsReplay replay{limits()};

    EXPECT_THROW(
        replay.run({submit_owned(2, make_order(1, Side::Buy, 1, 100))}),
        std::invalid_argument);

    EXPECT_THROW(
        replay.run({
            submit_owned(1, make_order(1, Side::Buy, 1, 100)),
            submit_owned(3, make_order(2, Side::Buy, 1, 99))
        }),
        std::invalid_argument);
}

TEST(OmsReplayTest, CancelUnknownOrExternalThrows) {
    const OmsReplay replay{limits()};

    EXPECT_THROW(
        replay.run({cancel_owned(1, 1)}),
        std::invalid_argument);

    EXPECT_THROW(
        replay.run({
            add_external(1, make_order(100, Side::Sell, 1, 100)),
            cancel_owned(2, 100)
        }),
        std::invalid_argument);
}

TEST(OmsReplayTest, CancelInactiveOwnedThrows) {
    EXPECT_THROW(
        OmsReplay{limits()}.run({
            submit_owned(1, make_order(1, Side::Buy, 1, 99)),
            cancel_owned(2, 1),
            cancel_owned(3, 1)
        }),
        std::logic_error);
}

TEST(OmsReplayTest, DirectOmsExecutionMatchesReplayOutputs) {
    const auto commands = mixed_sequence();
    const auto replayed = OmsReplay{limits()}.run(commands);
    const auto direct = apply_directly(limits(), commands);
    expect_same_result(replayed, direct);

    EXPECT_EQ(replayed.position, 5);
    EXPECT_EQ(replayed.trades.size(), 2U);
    EXPECT_FALSE(replayed.audit_events.empty());
    EXPECT_EQ(replayed.audit_events[0].sequence, 1U);
}

TEST(OmsReplayTest, RejectedCommandsDoNotOccupyOwnedIds) {
    const auto result = OmsReplay{limits()}.run({
        submit_owned(1, make_order(1, Side::Buy, 11, 100)),
        submit_owned(2, make_order(1, Side::Buy, 2, 100))
    });

    EXPECT_FALSE(result.commands[0].accepted);
    EXPECT_TRUE(result.commands[1].accepted);
    EXPECT_EQ(result.active_owned_orders, 1U);
    ASSERT_EQ(result.remaining_owned_orders.size(), 1U);
    EXPECT_EQ(result.remaining_owned_orders[0].id.value(), 1U);
}

}  // namespace
}  // namespace apexhft
