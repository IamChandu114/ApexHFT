#include "apexhft/domain.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

using namespace apexhft;

TEST(DomainTest, AcceptsValidValues) {
    EXPECT_EQ(PriceTicks{125}.value(), 125);
    EXPECT_EQ(Quantity{10}.value(), 10);
    EXPECT_EQ(OrderId{7}.value(), 7);
    EXPECT_EQ(TradeId{9}.value(), 9);
}

TEST(DomainTest, RejectsInvalidValues) {
    EXPECT_THROW(PriceTicks{0}, std::invalid_argument);
    EXPECT_THROW(Quantity{0}, std::invalid_argument);
    EXPECT_THROW(OrderId{0}, std::invalid_argument);
    EXPECT_THROW(TradeId{0}, std::invalid_argument);
}

TEST(DomainTest, AcceptsValidLimitOrder) {
    const Order order{OrderId{1}, Side::Buy, OrderType::Limit,
                      Quantity{5}, PriceTicks{100}};
    EXPECT_EQ(order.status(), OrderStatus::New);
    ASSERT_TRUE(order.limit_price().has_value());
    EXPECT_EQ(order.limit_price()->value(), 100);
    EXPECT_EQ(order.remaining_quantity(), 5);
}

TEST(DomainTest, RejectsInvalidOrderPrices) {
    EXPECT_THROW((Order{OrderId{1}, Side::Buy, OrderType::Limit, Quantity{5}}),
                 std::invalid_argument);
    EXPECT_THROW((Order{OrderId{1}, Side::Buy, OrderType::Market,
                        Quantity{5}, PriceTicks{100}}),
                 std::invalid_argument);
}

TEST(DomainTest, AcceptsMarketOrderWithoutPrice) {
    const Order order{OrderId{2}, Side::Sell, OrderType::Market, Quantity{3}};
    EXPECT_FALSE(order.limit_price().has_value());
}

TEST(DomainTest, ComparesPricesInTicks) {
    EXPECT_TRUE(PriceTicks{100} < PriceTicks{101});
    EXPECT_EQ(PriceTicks{100}, PriceTicks{100});
}

TEST(DomainTest, TracksPartialAndCompleteFills) {
    Order order{OrderId{1}, Side::Buy, OrderType::Limit,
                Quantity{10}, PriceTicks{100}};
    order.record_fill(Quantity{4});
    EXPECT_EQ(order.status(), OrderStatus::PartiallyFilled);
    EXPECT_EQ(order.filled_quantity(), 4);
    EXPECT_EQ(order.remaining_quantity(), 6);

    order.record_fill(Quantity{6});
    EXPECT_EQ(order.status(), OrderStatus::Filled);
    EXPECT_EQ(order.remaining_quantity(), 0);
}

TEST(DomainTest, RejectsOverfill) {
    Order order{OrderId{1}, Side::Buy, OrderType::Limit,
                Quantity{5}, PriceTicks{100}};
    EXPECT_THROW(order.record_fill(Quantity{6}), std::invalid_argument);
    EXPECT_EQ(order.remaining_quantity(), 5);
}

TEST(DomainTest, CannotFillOrCancelFilledOrder) {
    Order order{OrderId{1}, Side::Buy, OrderType::Limit,
                Quantity{2}, PriceTicks{100}};
    order.record_fill(Quantity{2});
    EXPECT_THROW(order.record_fill(Quantity{1}), std::logic_error);
    EXPECT_THROW(order.cancel(), std::logic_error);
}

TEST(DomainTest, CancelsPartiallyFilledOrder) {
    Order order{OrderId{1}, Side::Sell, OrderType::Limit,
                Quantity{5}, PriceTicks{100}};
    order.record_fill(Quantity{2});
    order.cancel();
    EXPECT_EQ(order.status(), OrderStatus::Cancelled);
    EXPECT_EQ(order.filled_quantity(), 2);
    EXPECT_EQ(order.remaining_quantity(), 3);
}

TEST(DomainTest, RejectsOnlyNewOrders) {
    Order order{OrderId{1}, Side::Buy, OrderType::Market, Quantity{2}};
    order.reject();
    EXPECT_EQ(order.status(), OrderStatus::Rejected);
    EXPECT_THROW(order.cancel(), std::logic_error);
}
