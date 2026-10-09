#pragma once

#include "apexhft/domain.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace apexhft {

struct RiskLimits {
    std::uint64_t max_order_quantity{100};
    std::int64_t max_abs_position{500};
    std::int64_t min_price_ticks{1};
    std::int64_t max_price_ticks{1'000'000};
};

class RiskManager {
public:
    explicit RiskManager(RiskLimits limits) : limits_(limits) {
        if (limits_.max_order_quantity == 0 ||
            limits_.max_abs_position < 0 ||
            limits_.min_price_ticks <= 0 ||
            limits_.max_price_ticks < limits_.min_price_ticks) {
            throw std::invalid_argument("Invalid risk limits");
        }
    }

    [[nodiscard]] std::optional<std::string> check(
        const Order& order, std::int64_t current_position) const {
        return check_with_pending_exposure(order, current_position, 0, 0);
    }

    [[nodiscard]] std::optional<std::string> check_with_pending_exposure(
        const Order& order,
        std::int64_t current_position,
        std::uint64_t pending_buys,
        std::uint64_t pending_sells) const {
        if (order.quantity().value() > limits_.max_order_quantity) {
            return "Order quantity exceeds limit";
        }

        if (order.type() == OrderType::Limit) {
            const auto price = order.limit_price()->value();
            if (price < limits_.min_price_ticks ||
                price > limits_.max_price_ticks) {
                return "Limit price outside permitted range";
            }
        }

        const auto quantity = order.quantity().value();

        auto long_exposure =
            apply_quantity(current_position, pending_buys, true);
        auto short_exposure =
            apply_quantity(current_position, pending_sells, false);

        if (!long_exposure || !short_exposure) {
            return "Projected position overflows";
        }

        if (order.side() == Side::Buy) {
            long_exposure =
                apply_quantity(*long_exposure, quantity, true);
        } else {
            short_exposure =
                apply_quantity(*short_exposure, quantity, false);
        }

        if (!long_exposure || !short_exposure) {
            return "Projected position overflows";
        }

        const auto limit = limits_.max_abs_position;
        if (*long_exposure > limit || *short_exposure < -limit) {
            return "Worst-case position exceeds limit";
        }

        return std::nullopt;
    }

private:
    RiskLimits limits_;

    [[nodiscard]] static std::optional<std::int64_t> apply_quantity(
        std::int64_t position, std::uint64_t quantity, bool add) {
        if (quantity > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }

        const auto amount = static_cast<std::int64_t>(quantity);

        if (add) {
            if (position >
                std::numeric_limits<std::int64_t>::max() - amount) {
                return std::nullopt;
            }
            return position + amount;
        }

        if (position <
            std::numeric_limits<std::int64_t>::min() + amount) {
            return std::nullopt;
        }
        return position - amount;
    }
};

}  // namespace apexhft
