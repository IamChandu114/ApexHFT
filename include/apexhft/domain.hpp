#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace apexhft {

class PriceTicks {
public:
    explicit PriceTicks(std::int64_t value) : value_(value) {
        if (value <= 0) throw std::invalid_argument("Price must be positive");
    }
    [[nodiscard]] std::int64_t value() const noexcept { return value_; }
    friend bool operator==(PriceTicks, PriceTicks) = default;
    friend auto operator<=>(PriceTicks, PriceTicks) = default;
private:
    std::int64_t value_;
};

class Quantity {
public:
    explicit Quantity(std::uint64_t value) : value_(value) {
        if (value == 0) throw std::invalid_argument("Quantity must be positive");
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }
private:
    std::uint64_t value_;
};

class OrderId {
public:
    explicit OrderId(std::uint64_t value) : value_(value) {
        if (value == 0) throw std::invalid_argument("Order ID must be nonzero");
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }
private:
    std::uint64_t value_;
};

class TradeId {
public:
    explicit TradeId(std::uint64_t value) : value_(value) {
        if (value == 0) throw std::invalid_argument("Trade ID must be nonzero");
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }
private:
    std::uint64_t value_;
};

enum class Side { Buy, Sell };
enum class OrderType { Limit, Market };
enum class OrderStatus { New, PartiallyFilled, Filled, Cancelled, Rejected };

class Order {
public:
    Order(OrderId id, Side side, OrderType type, Quantity quantity,
          std::optional<PriceTicks> price = std::nullopt)
        : id_(id), side_(side), type_(type), quantity_(quantity),
          limit_price_(price) {
        if (type_ == OrderType::Limit && !limit_price_)
            throw std::invalid_argument("Limit order requires a price");
        if (type_ == OrderType::Market && limit_price_)
            throw std::invalid_argument("Market order cannot have a limit price");
    }

    [[nodiscard]] OrderId id() const noexcept { return id_; }
    [[nodiscard]] Side side() const noexcept { return side_; }
    [[nodiscard]] OrderType type() const noexcept { return type_; }
    [[nodiscard]] Quantity quantity() const noexcept { return quantity_; }
    [[nodiscard]] const std::optional<PriceTicks>& limit_price() const noexcept {
        return limit_price_;
    }
    [[nodiscard]] OrderStatus status() const noexcept { return status_; }
    [[nodiscard]] std::uint64_t filled_quantity() const noexcept { return filled_; }
    [[nodiscard]] std::uint64_t remaining_quantity() const noexcept {
        return quantity_.value() - filled_;
    }

    void record_fill(Quantity fill) {
        if (status_ != OrderStatus::New &&
            status_ != OrderStatus::PartiallyFilled)
            throw std::logic_error("Cannot fill an inactive order");
        if (fill.value() > remaining_quantity())
            throw std::invalid_argument("Fill exceeds remaining quantity");

        filled_ += fill.value();
        status_ = (remaining_quantity() == 0)
            ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
    }

    void cancel() {
        if (status_ != OrderStatus::New &&
            status_ != OrderStatus::PartiallyFilled)
            throw std::logic_error("Cannot cancel an inactive order");
        status_ = OrderStatus::Cancelled;
    }

    void reject() {
        if (status_ != OrderStatus::New)
            throw std::logic_error("Only new orders can be rejected");
        status_ = OrderStatus::Rejected;
    }

private:
    OrderId id_;
    Side side_;
    OrderType type_;
    Quantity quantity_;
    std::optional<PriceTicks> limit_price_;
    OrderStatus status_{OrderStatus::New};
    std::uint64_t filled_{0};
};

}  // namespace apexhft
