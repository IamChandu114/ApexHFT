#pragma once

#include "apexhft/domain.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace apexhft {

struct Trade {
    TradeId id;
    OrderId buy_order_id;
    OrderId sell_order_id;
    PriceTicks price;
    Quantity quantity;
};

class MatchingEngine {
public:
    // Reference implementation: limit orders only.
    // Trades execute at the resting order's price.
    [[nodiscard]] std::vector<Trade> submit(Order order) {
        if (order.type() != OrderType::Limit)
            throw std::invalid_argument("Reference engine supports limit orders only");

        const auto id = order.id().value();
        if (orders_.contains(id))
            throw std::invalid_argument("Duplicate order ID");

        orders_.emplace(id, std::move(order));
        Order& incoming = orders_.at(id);
        std::vector<Trade> trades;

        while (incoming.remaining_quantity() > 0) {
            clean_best_ask();
            clean_best_bid();

            std::uint64_t resting_id = 0;
            std::int64_t trade_price = 0;
            const auto limit = incoming.limit_price()->value();

            if (incoming.side() == Side::Buy) {
                if (asks_.empty() || asks_.begin()->first > limit)
                    break;
                resting_id = asks_.begin()->second.front();
                trade_price = asks_.begin()->first;
            } else {
                if (bids_.empty() || bids_.begin()->first < limit)
                    break;
                resting_id = bids_.begin()->second.front();
                trade_price = bids_.begin()->first;
            }

            Order& resting = orders_.at(resting_id);
            const auto fill_qty = std::min(
                incoming.remaining_quantity(),
                resting.remaining_quantity());

            incoming.record_fill(Quantity{fill_qty});
            resting.record_fill(Quantity{fill_qty});

            const auto buy_id = incoming.side() == Side::Buy ? id : resting_id;
            const auto sell_id = incoming.side() == Side::Sell ? id : resting_id;

            trades.push_back(Trade{
                TradeId{next_trade_id_++},
                OrderId{buy_id},
                OrderId{sell_id},
                PriceTicks{trade_price},
                Quantity{fill_qty}
            });

            if (resting.status() == OrderStatus::Filled)
                remove_filled_front(resting.side(), trade_price);
        }

        if (incoming.remaining_quantity() > 0)
            add_to_book(incoming);

        return trades;
    }

    void cancel(OrderId id) {
        const auto it = orders_.find(id.value());
        if (it == orders_.end())
            throw std::out_of_range("Unknown order ID");

        Order& order = it->second;
        const auto side = order.side();
        const auto price = order.limit_price()->value();

        order.cancel();
        remove_from_level(side, price, id.value());
    }

    [[nodiscard]] const Order& get_order(OrderId id) const {
        const auto it = orders_.find(id.value());
        if (it == orders_.end())
            throw std::out_of_range("Unknown order ID");
        return it->second;
    }

    [[nodiscard]] std::optional<PriceTicks> best_bid() const {
        if (bids_.empty()) return std::nullopt;
        return PriceTicks{bids_.begin()->first};
    }

    [[nodiscard]] std::optional<PriceTicks> best_ask() const {
        if (asks_.empty()) return std::nullopt;
        return PriceTicks{asks_.begin()->first};
    }

    [[nodiscard]] std::size_t active_order_count() const noexcept {
        std::size_t count = 0;
        for (const auto& [id, order] : orders_) {
            (void)id;
            if (order.status() == OrderStatus::New ||
                order.status() == OrderStatus::PartiallyFilled)
                ++count;
        }
        return count;
    }

private:
    using AskBook = std::map<std::int64_t, std::deque<std::uint64_t>>;
    using BidBook = std::map<std::int64_t, std::deque<std::uint64_t>,
                              std::greater<>>;

    std::unordered_map<std::uint64_t, Order> orders_;
    BidBook bids_;
    AskBook asks_;
    std::uint64_t next_trade_id_{1};

    void add_to_book(const Order& order) {
        const auto price = order.limit_price()->value();
        if (order.side() == Side::Buy)
            bids_[price].push_back(order.id().value());
        else
            asks_[price].push_back(order.id().value());
    }

    void remove_from_level(Side side, std::int64_t price, std::uint64_t id) {
        if (side == Side::Buy) {
            auto level = bids_.find(price);
            if (level == bids_.end()) return;
            auto& queue = level->second;
            queue.erase(std::remove(queue.begin(), queue.end(), id), queue.end());
            if (queue.empty()) bids_.erase(level);
        } else {
            auto level = asks_.find(price);
            if (level == asks_.end()) return;
            auto& queue = level->second;
            queue.erase(std::remove(queue.begin(), queue.end(), id), queue.end());
            if (queue.empty()) asks_.erase(level);
        }
    }

    void remove_filled_front(Side side, std::int64_t price) {
        if (side == Side::Buy) {
            auto level = bids_.find(price);
            if (level == bids_.end()) return;
            level->second.pop_front();
            if (level->second.empty()) bids_.erase(level);
        } else {
            auto level = asks_.find(price);
            if (level == asks_.end()) return;
            level->second.pop_front();
            if (level->second.empty()) asks_.erase(level);
        }
    }

    void clean_best_ask() {
        while (!asks_.empty() && asks_.begin()->second.empty())
            asks_.erase(asks_.begin());
    }

    void clean_best_bid() {
        while (!bids_.empty() && bids_.begin()->second.empty())
            bids_.erase(bids_.begin());
    }
};

}  // namespace apexhft
