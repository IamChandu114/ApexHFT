#pragma once

#include "apexhft/event_log.hpp"
#include "apexhft/matching_engine.hpp"
#include "apexhft/risk.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace apexhft {

struct SubmissionResult {
    bool accepted{false};
    std::string reason;
    std::vector<Trade> trades;
};

class OrderManagementSystem {
public:
    explicit OrderManagementSystem(RiskLimits limits) : risk_(limits) {}

    [[nodiscard]] SubmissionResult submit(Order order) {
        if (order.type() != OrderType::Limit) {
            return reject("OMS currently supports limit orders only", order.id());
        }

        for (const auto id : active_ids_) {
            const auto& resting = engine_.get_order(id);
            if (resting.status() != OrderStatus::New &&
                resting.status() != OrderStatus::PartiallyFilled) {
                continue;
            }
            if (resting.side() == order.side()) {
                continue;
            }

            const auto a = resting.limit_price()->value();
            const auto b = order.limit_price()->value();
            if ((order.side() == Side::Buy && b >= a) ||
                (order.side() == Side::Sell && b <= a)) {
                return reject("Self-cross prevented", order.id());
            }
        }

        if (const auto reason = risk_.check_with_pending_exposure(
                order, position_, pending_quantity(Side::Buy),
                pending_quantity(Side::Sell))) {
            return reject(*reason, order.id());
        }

        const auto id = order.id();
        auto trades = engine_.submit(std::move(order));
        order_ids_.push_back(id);
        owned_ids_.insert(id.value());
        refresh_active(id);
        event_log_.append(EventType::OrderAccepted,
            "order_id=" + std::to_string(id.value()));
        apply_trades(trades);
        return {true, {}, std::move(trades)};
    }

    [[nodiscard]] std::vector<Trade> add_external_liquidity(Order order) {
        if (order.type() != OrderType::Limit) {
            throw std::invalid_argument(
                "External liquidity must be a limit order");
        }

        auto trades = engine_.submit(std::move(order));
        prune_active();
        apply_trades(trades);
        return trades;
    }

    void cancel(OrderId id) {
        if (!is_own_order(id)) {
            throw std::invalid_argument("Cannot cancel a non-owned order");
        }
        engine_.cancel(id);
        prune_active();
        event_log_.append(EventType::OrderCancelled,
            "order_id=" + std::to_string(id.value()));
    }

    [[nodiscard]] const EventLog& event_log() const noexcept {
        return event_log_;
    }

    [[nodiscard]] std::int64_t position() const noexcept {
        return position_;
    }

    [[nodiscard]] const Order& get_order(OrderId id) const {
        return engine_.get_order(id);
    }

    [[nodiscard]] std::size_t active_order_count() const noexcept {
        return active_ids_.size();
    }

    // Owned IDs in submission order, including filled and cancelled orders.
    // Rejected submissions are not recorded.
    [[nodiscard]] const std::vector<OrderId>& owned_order_ids() const noexcept {
        return order_ids_;
    }

private:
    RiskManager risk_;
    EventLog event_log_;
    MatchingEngine engine_;
    std::vector<OrderId> order_ids_;
    std::vector<OrderId> active_ids_;
    std::unordered_set<std::uint64_t> owned_ids_;
    std::int64_t position_{0};

    [[nodiscard]] SubmissionResult reject(
        const std::string& reason, OrderId id) {
        event_log_.append(EventType::OrderRejected,
            "order_id=" + std::to_string(id.value()) +
            ";reason=" + reason);
        return {false, reason, {}};
    }

    [[nodiscard]] bool is_own_order(OrderId id) const {
        return owned_ids_.contains(id.value());
    }

    void prune_active() {
        active_ids_.erase(
            std::remove_if(active_ids_.begin(), active_ids_.end(),
                [this](OrderId id) {
                    const auto status = engine_.get_order(id).status();
                    return status != OrderStatus::New &&
                           status != OrderStatus::PartiallyFilled;
                }),
            active_ids_.end());
    }

    void refresh_active(OrderId id) {
        prune_active();
        const auto& order = engine_.get_order(id);
        if (order.status() == OrderStatus::New ||
            order.status() == OrderStatus::PartiallyFilled) {
            active_ids_.push_back(id);
        }
    }

    [[nodiscard]] std::uint64_t pending_quantity(Side side) const {
        std::uint64_t total = 0;

        for (const auto id : active_ids_) {
            const auto& order = engine_.get_order(id);
            if (order.side() != side ||
                (order.status() != OrderStatus::New &&
                 order.status() != OrderStatus::PartiallyFilled)) {
                continue;
            }

            const auto remaining = order.remaining_quantity();
            if (remaining >
                std::numeric_limits<std::uint64_t>::max() - total) {
                throw std::overflow_error("Pending quantity overflows");
            }
            total += remaining;
        }

        return total;
    }

    void apply_trades(const std::vector<Trade>& trades) {
        for (const auto& trade : trades) {
            event_log_.append(EventType::TradeExecuted,
                "trade_id=" + std::to_string(trade.id.value()) +
                ";buy_order_id=" + std::to_string(trade.buy_order_id.value()) +
                ";sell_order_id=" + std::to_string(trade.sell_order_id.value()) +
                ";price_ticks=" + std::to_string(trade.price.value()) +
                ";quantity=" + std::to_string(trade.quantity.value()));
            if (trade.quantity.value() > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                throw std::overflow_error("Trade quantity overflows");
            }

            const auto qty =
                static_cast<std::int64_t>(trade.quantity.value());

            if (is_own_order(trade.buy_order_id)) {
                position_ = checked_add(position_, qty);
            }
            if (is_own_order(trade.sell_order_id)) {
                position_ = checked_add(position_, -qty);
            }
        }
    }

    [[nodiscard]] static std::int64_t checked_add(
        std::int64_t left, std::int64_t right) {
        if (right > 0 &&
            left > std::numeric_limits<std::int64_t>::max() - right) {
            throw std::overflow_error("Position overflows");
        }
        if (right < 0 &&
            left < std::numeric_limits<std::int64_t>::min() - right) {
            throw std::overflow_error("Position overflows");
        }
        return left + right;
    }
};

}  // namespace apexhft
