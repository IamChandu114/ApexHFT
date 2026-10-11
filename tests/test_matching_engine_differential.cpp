#include "apexhft/matching_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

namespace apexhft {
namespace {

struct RefOrder {
    std::uint64_t id;
    Side side;
    std::uint64_t quantity;
    std::int64_t price;
    std::uint64_t filled{0};
    OrderStatus status{OrderStatus::New};

    [[nodiscard]] std::uint64_t remaining() const noexcept {
        return quantity - filled;
    }

    [[nodiscard]] bool active() const noexcept {
        return status == OrderStatus::New ||
               status == OrderStatus::PartiallyFilled;
    }
};

struct Operation {
    enum class Kind { Submit, Cancel };

    Kind kind;
    std::uint64_t id;
    Side side{Side::Buy};
    std::uint64_t quantity{0};
    std::int64_t price{0};

    [[nodiscard]] std::string describe() const {
        std::ostringstream out;
        if (kind == Kind::Cancel) {
            out << "cancel id=" << id;
        } else {
            out << "submit id=" << id
                << " side=" << (side == Side::Buy ? "buy" : "sell")
                << " quantity=" << quantity << " price=" << price;
        }
        return out.str();
    }
};

[[nodiscard]] std::string format_failure_trace(
    std::uint64_t seed,
    const std::vector<Operation>& history,
    std::optional<std::size_t> failing_op_index = std::nullopt,
    std::string_view failure_reason = "") {
    std::ostringstream out;
    out << "Differential failure trace: seed=" << seed
        << ", operations_executed=" << history.size() << '\n';
    if (failing_op_index.has_value()) {
        out << "failing_operation_index=" << *failing_op_index << '\n';
        if (*failing_op_index < history.size()) {
            out << "failing_operation=" << history[*failing_op_index].describe() << '\n';
        }
    }
    if (!failure_reason.empty()) {
        out << "failure_reason=" << failure_reason << '\n';
    }
    if (history.size() <= 5000) {
        for (std::size_t i = 0; i < history.size(); ++i) {
            out << i << ": " << history[i].describe() << '\n';
        }
    } else {
        for (std::size_t i = 0; i < 50; ++i) {
            out << i << ": " << history[i].describe() << '\n';
        }
        out << "... [" << (history.size() - 550) << " operations omitted] ...\n";
        for (std::size_t i = history.size() - 500; i < history.size(); ++i) {
            out << i << ": " << history[i].describe() << '\n';
        }
    }
    return out.str();
}

struct ExpectedTrade {
    std::uint64_t id;
    std::uint64_t buy_id;
    std::uint64_t sell_id;
    std::int64_t price;
    std::uint64_t quantity;
};

class ReferenceBook {
public:
    std::vector<ExpectedTrade> submit(
        std::uint64_t id, Side side, std::uint64_t quantity,
        std::int64_t price) {
        orders_.push_back(RefOrder{id, side, quantity, price});
        auto& incoming = orders_.back();
        std::vector<ExpectedTrade> trades;

        while (incoming.remaining() > 0) {
            if (side == Side::Buy) {
                if (ask_levels_.empty())
                    break;
                auto best_it = ask_levels_.begin();
                if (best_it->first > price)
                    break;

                const auto resting_id = best_it->second.front();
                auto& resting = orders_[resting_id - 1];
                const auto fill = std::min(incoming.remaining(), resting.remaining());

                trades.push_back(ExpectedTrade{
                    next_trade_id_++,
                    id,
                    resting_id,
                    resting.price,
                    fill
                });

                incoming.filled += fill;
                resting.filled += fill;
                incoming.status = (incoming.remaining() == 0)
                    ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
                resting.status = (resting.remaining() == 0)
                    ? OrderStatus::Filled : OrderStatus::PartiallyFilled;

                if (resting.status == OrderStatus::Filled) {
                    best_it->second.erase(best_it->second.begin());
                    if (best_it->second.empty()) {
                        ask_levels_.erase(best_it);
                    }
                    remove_active_id(resting_id);
                }
            } else {
                if (bid_levels_.empty())
                    break;
                auto best_it = bid_levels_.begin();
                if (best_it->first < price)
                    break;

                const auto resting_id = best_it->second.front();
                auto& resting = orders_[resting_id - 1];
                const auto fill = std::min(incoming.remaining(), resting.remaining());

                trades.push_back(ExpectedTrade{
                    next_trade_id_++,
                    resting_id,
                    id,
                    resting.price,
                    fill
                });

                incoming.filled += fill;
                resting.filled += fill;
                incoming.status = (incoming.remaining() == 0)
                    ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
                resting.status = (resting.remaining() == 0)
                    ? OrderStatus::Filled : OrderStatus::PartiallyFilled;

                if (resting.status == OrderStatus::Filled) {
                    best_it->second.erase(best_it->second.begin());
                    if (best_it->second.empty()) {
                        bid_levels_.erase(best_it);
                    }
                    remove_active_id(resting_id);
                }
            }
        }

        if (incoming.active()) {
            if (side == Side::Buy) {
                bid_levels_[price].push_back(id);
            } else {
                ask_levels_[price].push_back(id);
            }
            add_active_id(id);
        }

        return trades;
    }

    void cancel(std::uint64_t id) {
        auto* order = find(id);
        if (order && order->active()) {
            order->status = OrderStatus::Cancelled;
            if (order->side == Side::Buy) {
                auto it = bid_levels_.find(order->price);
                if (it != bid_levels_.end()) {
                    auto& queue = it->second;
                    queue.erase(std::remove(queue.begin(), queue.end(), id), queue.end());
                    if (queue.empty()) {
                        bid_levels_.erase(it);
                    }
                }
            } else {
                auto it = ask_levels_.find(order->price);
                if (it != ask_levels_.end()) {
                    auto& queue = it->second;
                    queue.erase(std::remove(queue.begin(), queue.end(), id), queue.end());
                    if (queue.empty()) {
                        ask_levels_.erase(it);
                    }
                }
            }
            remove_active_id(id);
        }
    }

    [[nodiscard]] const std::vector<RefOrder>& orders() const noexcept {
        return orders_;
    }

    [[nodiscard]] const std::vector<std::uint64_t>& active_ids() const noexcept {
        return active_ids_;
    }

    struct BookSnapshot {
        std::size_t active_count{0};
        std::optional<std::int64_t> best_bid;
        std::optional<std::int64_t> best_ask;
    };

    [[nodiscard]] BookSnapshot snapshot() const {
        BookSnapshot result;
        result.active_count = active_ids_.size();
        if (!bid_levels_.empty()) {
            result.best_bid = bid_levels_.begin()->first;
        }
        if (!ask_levels_.empty()) {
            result.best_ask = ask_levels_.begin()->first;
        }
        return result;
    }

    [[nodiscard]] std::size_t live_count() const noexcept {
        return active_ids_.size();
    }

    [[nodiscard]] const RefOrder* find(std::uint64_t id) const noexcept {
        if (id >= 1 && id <= orders_.size())
            return &orders_[id - 1];
        return nullptr;
    }

    [[nodiscard]] RefOrder* find(std::uint64_t id) noexcept {
        if (id >= 1 && id <= orders_.size())
            return &orders_[id - 1];
        return nullptr;
    }

private:
    std::vector<RefOrder> orders_;
    std::map<std::int64_t, std::vector<std::uint64_t>, std::greater<std::int64_t>> bid_levels_;
    std::map<std::int64_t, std::vector<std::uint64_t>> ask_levels_;
    std::vector<std::uint64_t> active_ids_;
    std::unordered_map<std::uint64_t, std::size_t> active_id_to_idx_;
    std::uint64_t next_trade_id_{1};

    void add_active_id(std::uint64_t id) {
        active_id_to_idx_[id] = active_ids_.size();
        active_ids_.push_back(id);
    }

    void remove_active_id(std::uint64_t id) {
        auto it = active_id_to_idx_.find(id);
        if (it == active_id_to_idx_.end())
            return;
        const auto idx = it->second;
        const auto last_id = active_ids_.back();
        active_ids_[idx] = last_id;
        active_id_to_idx_[last_id] = idx;
        active_ids_.pop_back();
        active_id_to_idx_.erase(it);
    }
};

std::uint64_t operation_count_from_environment() {
    const char* value = std::getenv("APEXHFT_DIFF_OPS");
    if (value == nullptr || *value == '\0')
        return 5000;

    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0' || parsed == 0)
        throw std::invalid_argument(
            "APEXHFT_DIFF_OPS must be a positive integer");
    return parsed;
}

TEST(MatchingEngineDifferentialTest, FailureTraceIsReplayable) {
    const std::vector<Operation> history{
        {Operation::Kind::Submit, 1, Side::Buy, 2, 100},
        {Operation::Kind::Cancel, 1},
    };
    constexpr std::uint64_t seed = 0xD1FF3E71A1ULL;
    const auto text = format_failure_trace(seed, history);
    EXPECT_NE(text.find("seed=" + std::to_string(seed)), std::string::npos);
    EXPECT_NE(text.find("operations_executed=2"), std::string::npos);
    EXPECT_NE(text.find('\n'), std::string::npos);
    EXPECT_EQ(text.find('\\'), std::string::npos);
    EXPECT_NE(text.find("0: submit id=1 side=buy quantity=2 price=100"),
              std::string::npos);
    EXPECT_NE(text.find("1: cancel id=1"), std::string::npos);
}

TEST(MatchingEngineDifferentialTest, FailureTraceIncludesDiagnosticsAndReason) {
    const std::vector<Operation> history{
        {Operation::Kind::Submit, 1, Side::Buy, 2, 100},
        {Operation::Kind::Cancel, 1},
    };
    constexpr std::uint64_t seed = 0xD1FF3E71A1ULL;
    const auto text = format_failure_trace(
        seed, history, 1, "Order status mismatch: expected Cancelled, got New");
    EXPECT_NE(text.find("failing_operation_index=1"), std::string::npos);
    EXPECT_NE(text.find("failing_operation=cancel id=1"), std::string::npos);
    EXPECT_NE(text.find("failure_reason=Order status mismatch"), std::string::npos);
}

TEST(MatchingEngineDifferentialTest, DiagnosticCapturesDeliberateMismatch) {
    const std::vector<Operation> history{
        {Operation::Kind::Submit, 1, Side::Buy, 5, 100},
        {Operation::Kind::Submit, 2, Side::Sell, 3, 100},
    };
    constexpr std::uint64_t seed = 0xD1FF3E71A1ULL;
    const std::string reason = "Trade quantity mismatch: engine=3 reference=4";
    const auto trace = format_failure_trace(seed, history, 1, reason);
    EXPECT_NE(trace.find("seed=" + std::to_string(seed)), std::string::npos);
    EXPECT_NE(trace.find("operations_executed=2"), std::string::npos);
    EXPECT_NE(trace.find("failing_operation_index=1"), std::string::npos);
    EXPECT_NE(trace.find("failing_operation=submit id=2 side=sell quantity=3 price=100"),
              std::string::npos);
    EXPECT_NE(trace.find("failure_reason=" + reason), std::string::npos);
}

TEST(MatchingEngineDifferentialTest, ReplayableTraceReproducesState) {
    const std::vector<Operation> history{
        {Operation::Kind::Submit, 1, Side::Buy, 10, 100},
        {Operation::Kind::Submit, 2, Side::Sell, 4, 100},
        {Operation::Kind::Submit, 3, Side::Buy, 5, 99},
        {Operation::Kind::Cancel, 3},
    };

    MatchingEngine engine;
    ReferenceBook reference;

    for (const auto& op : history) {
        if (op.kind == Operation::Kind::Cancel) {
            reference.cancel(op.id);
            engine.cancel(OrderId{op.id});
        } else {
            const auto expected = reference.submit(op.id, op.side, op.quantity, op.price);
            const auto actual = engine.submit(Order{
                OrderId{op.id}, op.side, OrderType::Limit,
                Quantity{op.quantity}, PriceTicks{op.price}});
            ASSERT_EQ(actual.size(), expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                EXPECT_EQ(actual[i].price.value(), expected[i].price);
                EXPECT_EQ(actual[i].quantity.value(), expected[i].quantity);
            }
        }
    }

    EXPECT_EQ(engine.active_order_count(), reference.live_count());
    EXPECT_EQ(engine.best_bid().value().value(), 100);
    EXPECT_EQ(engine.get_order(OrderId{1}).remaining_quantity(), 6U);
    EXPECT_EQ(engine.get_order(OrderId{2}).status(), OrderStatus::Filled);
    EXPECT_EQ(engine.get_order(OrderId{3}).status(), OrderStatus::Cancelled);
}

TEST(MatchingEngineDifferentialTest, SubmitAndCancelMatchReferenceModel) {
    constexpr std::uint64_t seed = 0xD1FF3E71A1ULL;
    const auto operation_count = operation_count_from_environment();

    MatchingEngine engine;
    ReferenceBook reference;
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> action_dist{0, 99};
    std::uniform_int_distribution<int> side_dist{0, 1};
    std::uniform_int_distribution<int> quantity_dist{1, 10};
    std::uniform_int_distribution<int> price_dist{90, 110};

    std::vector<Operation> operation_history;
    operation_history.reserve(static_cast<std::size_t>(
        std::min<std::uint64_t>(operation_count, 2'000'000)));

    std::string failure_reason;
    struct FailureTraceGuard {
        const std::vector<Operation>& history;
        std::uint64_t seed;
        const std::string& reason;

        ~FailureTraceGuard() {
            if (!::testing::Test::HasFailure())
                return;
            const auto failing_idx = history.empty() ? 0 : history.size() - 1;
            std::cerr << format_failure_trace(seed, history, failing_idx, reason);
        }
    } failure_trace_guard{operation_history, seed, failure_reason};

    std::uint64_t next_order_id = 1;
    std::vector<std::uint64_t> touched;
    touched.reserve(16);

    for (std::uint64_t op = 0; op < operation_count; ++op) {
        touched.clear();
        const bool has_active_orders = reference.live_count() != 0;
        const bool do_cancel = has_active_orders && action_dist(rng) < 30;

        if (do_cancel) {
            const auto& active_ids = reference.active_ids();
            std::uniform_int_distribution<std::size_t> pick{
                0, active_ids.size() - 1};
            const auto id = active_ids[pick(rng)];
            touched.push_back(id);

            operation_history.push_back(
                Operation{Operation::Kind::Cancel, id});

            reference.cancel(id);
            engine.cancel(OrderId{id});
        } else {
            const auto id = next_order_id++;
            const auto side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
            const auto quantity =
                static_cast<std::uint64_t>(quantity_dist(rng));
            const auto price =
                static_cast<std::int64_t>(price_dist(rng));
            touched.push_back(id);

            operation_history.push_back(Operation{
                Operation::Kind::Submit, id, side, quantity, price});

            const auto expected =
                reference.submit(id, side, quantity, price);
            const auto actual = engine.submit(Order{
                OrderId{id}, side, OrderType::Limit, Quantity{quantity},
                PriceTicks{price}});

            if (actual.size() != expected.size()) {
                failure_reason = "Trade count mismatch: engine=" +
                                 std::to_string(actual.size()) +
                                 " expected=" + std::to_string(expected.size());
            }
            ASSERT_EQ(actual.size(), expected.size());

            for (std::size_t i = 0; i < expected.size(); ++i) {
                touched.push_back(expected[i].buy_id);
                touched.push_back(expected[i].sell_id);
                EXPECT_EQ(actual[i].id.value(), expected[i].id);
                EXPECT_EQ(actual[i].buy_order_id.value(), expected[i].buy_id);
                EXPECT_EQ(actual[i].sell_order_id.value(), expected[i].sell_id);
                EXPECT_EQ(actual[i].price.value(), expected[i].price);
                EXPECT_EQ(actual[i].quantity.value(), expected[i].quantity);
            }
        }

        // Verify touched orders (state changes)
        for (const auto id : touched) {
            const auto* expected = reference.find(id);
            if (!expected)
                continue;
            const auto& actual = engine.get_order(OrderId{id});
            EXPECT_EQ(actual.filled_quantity(), expected->filled);
            EXPECT_EQ(actual.remaining_quantity(), expected->remaining());
            EXPECT_EQ(actual.status(), expected->status);
        }

        // Verify active orders: on every operation for standard test runs (<= 10k ops),
        // or every 64 operations and the final operation for extended validation runs.
        const bool verify_active_book = (operation_count <= 10'000) ||
                                        (op % 64 == 0) ||
                                        (op + 1 == operation_count);
        if (verify_active_book) {
            for (const auto id : reference.active_ids()) {
                const auto* expected = reference.find(id);
                const auto& actual = engine.get_order(OrderId{id});
                EXPECT_EQ(actual.filled_quantity(), expected->filled);
                EXPECT_EQ(actual.remaining_quantity(), expected->remaining());
                EXPECT_EQ(actual.status(), expected->status);
            }
        }

        const auto snapshot = reference.snapshot();
        EXPECT_EQ(engine.active_order_count(), snapshot.active_count);

        const auto bid = engine.best_bid();
        const auto ask = engine.best_ask();
        ASSERT_EQ(bid.has_value(), snapshot.best_bid.has_value());
        ASSERT_EQ(ask.has_value(), snapshot.best_ask.has_value());
        if (bid && snapshot.best_bid) {
            EXPECT_EQ(bid->value(), *snapshot.best_bid);
        }
        if (ask && snapshot.best_ask) {
            EXPECT_EQ(ask->value(), *snapshot.best_ask);
        }
        if (bid && ask) {
            EXPECT_LT(bid->value(), ask->value());
        }
    }

    SUCCEED() << "seed=" << seed
              << ", operations=" << operation_count
              << ", final_active_orders=" << reference.snapshot().active_count;
}

}  // namespace
}  // namespace apexhft
