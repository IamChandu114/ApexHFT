#include "apexhft/research.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace apexhft {
namespace {

std::vector<QuoteObservation> parse(const std::string& text) {
    std::istringstream in(text);
    return load_quotes(in);
}

TEST(ResearchDataTest, LoadsValidQuotedSnapshot) {
    const auto quotes = parse(
        "# synthetic\n"
        "100,10,12,5,5\n"
        "200,11,13,4,4\n");
    ASSERT_EQ(quotes.size(), 2U);
    EXPECT_EQ(quotes[0].timestamp_ns, 100U);
    EXPECT_EQ(quotes[1].bid_ticks, 11);
}

TEST(ResearchDataTest, AllowsDuplicateTimestampsInFileOrder) {
    const auto quotes = parse("100,10,12,1,1\n100,9,11,1,1\n");
    ASSERT_EQ(quotes.size(), 2U);
    EXPECT_EQ(quotes[0].bid_ticks, 10);
    EXPECT_EQ(quotes[1].bid_ticks, 9);
}

TEST(ResearchDataTest, RejectsUnsortedTimestamps) {
    EXPECT_THROW(parse("200,10,12,1,1\n100,10,12,1,1\n"),
                 std::invalid_argument);
}

TEST(ResearchDataTest, RejectsMissingAndInvalidFields) {
    EXPECT_THROW(parse("100,10,12,1\n"), std::invalid_argument);
    EXPECT_THROW(parse("100,10,12,1,\n"), std::invalid_argument);
    EXPECT_THROW(parse("0,10,12,1,1\n"), std::invalid_argument);
    EXPECT_THROW(parse("100,-1,12,1,1\n"), std::invalid_argument);
    EXPECT_THROW(parse("100,10,10,1,1\n"), std::invalid_argument);
    EXPECT_THROW(parse("100,10,12,0,1\n"), std::invalid_argument);
    EXPECT_THROW(parse("abc,10,12,1,1\n"), std::invalid_argument);
}

TEST(ResearchDataTest, EmptyDatasetIsValid) {
    EXPECT_TRUE(parse("# only comments\n\n").empty());
}

TEST(ResearchStrategyTest, HoldsUntilPreviousMidExists) {
    StrategyConfig cfg;
    QuoteObservation quote{100, 10, 12, 1, 1};
    EXPECT_EQ(threshold_reversion_action(cfg, std::nullopt, quote, 0),
              StrategyAction::Hold);
}

TEST(ResearchStrategyTest, BuysAfterDownMoveAndSellsAfterUpMove) {
    StrategyConfig cfg;
    cfg.entry_ticks = 5;
    cfg.order_quantity = 1;
    cfg.max_abs_position = 1;
    QuoteObservation down{200, 90, 92, 10, 10};
    QuoteObservation up{300, 100, 102, 10, 10};
    EXPECT_EQ(threshold_reversion_action(cfg, 101, down, 0),
              StrategyAction::Buy);
    EXPECT_EQ(threshold_reversion_action(cfg, 91, up, 1),
              StrategyAction::Sell);
}

TEST(ResearchStrategyTest, RespectsPositionLimit) {
    StrategyConfig cfg;
    cfg.entry_ticks = 5;
    QuoteObservation down{200, 90, 92, 10, 10};
    EXPECT_EQ(threshold_reversion_action(cfg, 101, down, 1),
              StrategyAction::Hold);
}

TEST(ResearchBacktestTest, HandCalculatedRoundTrip) {
    // mid0=101, mid1=91 triggers buy 1 at ask 92, fee 1 => cash -93
    // mid2=101 triggers sell 1 at bid 100, fee 1 => cash 6
    const auto quotes = parse(
        "100,100,102,10,10\n"
        "200,90,92,10,10\n"
        "300,100,102,10,10\n");
    StrategyConfig strategy{5, 1, 1};
    ExecutionConfig execution{1, 0, RiskLimits{10, 10, 1, 1000}};
    const auto result = run_backtest(quotes, strategy, execution, "fixture", false);

    EXPECT_EQ(result.actions[0], StrategyAction::Hold);
    EXPECT_EQ(result.actions[1], StrategyAction::Buy);
    EXPECT_EQ(result.actions[2], StrategyAction::Sell);
    EXPECT_EQ(result.position, 0);
    EXPECT_EQ(result.cash_tick_lots, 6);
    EXPECT_EQ(result.final_equity_tick_lots, 6);
    EXPECT_EQ(result.transaction_costs_tick_lots, 2);
    EXPECT_EQ(result.trades, 2U);
    EXPECT_EQ(result.filled_quantity, 2U);
    EXPECT_EQ(result.submitted_quantity, 2U);
    EXPECT_DOUBLE_EQ(result.fill_rate, 1.0);
    ASSERT_EQ(result.fills.size(), 2U);
    EXPECT_EQ(result.fills[0].trade.price.value(), 92);
    EXPECT_EQ(result.fills[1].trade.price.value(), 100);
}

TEST(ResearchBacktestTest, AppliesFeesAndSlippage) {
    const auto quotes = parse(
        "100,100,102,10,10\n"
        "200,90,92,10,10\n");
    StrategyConfig strategy{5, 1, 1};
    ExecutionConfig execution{2, 1, RiskLimits{10, 10, 1, 1000}};
    const auto result = run_backtest(quotes, strategy, execution, "slip", false);

    ASSERT_EQ(result.fills.size(), 1U);
    EXPECT_EQ(result.fills[0].trade.price.value(), 93);
    EXPECT_EQ(result.cash_tick_lots, -95);
    EXPECT_EQ(result.position, 1);
}

TEST(ResearchBacktestTest, PartialFillFromDisplayedSize) {
    const auto quotes = parse(
        "100,100,102,10,10\n"
        "200,90,92,10,1\n");
    StrategyConfig strategy{5, 3, 3};
    ExecutionConfig execution{0, 0, RiskLimits{10, 10, 1, 1000}};
    const auto result = run_backtest(quotes, strategy, execution, "partial", false);

    EXPECT_EQ(result.filled_quantity, 1U);
    EXPECT_EQ(result.submitted_quantity, 3U);
    EXPECT_EQ(result.position, 1);
    EXPECT_GT(result.cancelled_residuals, 0U);
}

TEST(ResearchBacktestTest, RejectedOrderLeavesPositionUnchanged) {
    const auto quotes = parse(
        "100,100,102,10,10\n"
        "200,90,92,10,10\n");
    StrategyConfig strategy{5, 5, 5};
    ExecutionConfig execution{0, 0, RiskLimits{1, 10, 1, 1000}};
    const auto result = run_backtest(quotes, strategy, execution, "reject", false);

    EXPECT_GE(result.rejected_orders, 1U);
    EXPECT_EQ(result.position, 0);
    EXPECT_EQ(result.filled_quantity, 0U);
}

TEST(ResearchBacktestTest, EmptyQuotesProduceZeroMetrics) {
    const auto result = run_backtest(
        {}, StrategyConfig{}, ExecutionConfig{}, "empty", false);
    EXPECT_TRUE(result.equity_curve.empty());
    EXPECT_EQ(result.net_pnl_tick_lots, 0);
    EXPECT_EQ(result.max_drawdown_tick_lots, 0);
    EXPECT_EQ(result.equity_volatility, 0.0);
}

TEST(ResearchBacktestTest, RepeatableAndHasNoLookAhead) {
    const auto quotes = parse(
        "100,100,102,10,10\n"
        "200,90,92,10,10\n"
        "300,100,102,10,10\n"
        "400,80,82,10,10\n");
    StrategyConfig strategy{5, 1, 1};
    ExecutionConfig execution{1, 0, RiskLimits{10, 10, 1, 1000}};
    const auto a = run_backtest(quotes, strategy, execution, "d", false);
    const auto b = run_backtest(quotes, strategy, execution, "d", false);
    EXPECT_EQ(a.net_pnl_tick_lots, b.net_pnl_tick_lots);
    EXPECT_EQ(a.actions, b.actions);
    EXPECT_EQ(a.fills.size(), b.fills.size());

    const std::vector<QuoteObservation> prefix(quotes.begin(), quotes.begin() + 3);
    const auto prefix_run =
        run_backtest(prefix, strategy, execution, "p", false);
    ASSERT_EQ(prefix_run.actions.size(), 3U);
    EXPECT_EQ(prefix_run.actions, std::vector<StrategyAction>(
        a.actions.begin(), a.actions.begin() + 3));
}

TEST(ResearchMetricsTest, DrawdownAndZeroVariance) {
    EXPECT_EQ(max_drawdown({0, 10, 4, 12, 11}), 6);
    EXPECT_EQ(max_drawdown({}), 0);
    EXPECT_EQ(sample_stdev({}), 0.0);
    EXPECT_EQ(sample_stdev({3.0}), 0.0);
    EXPECT_EQ(sample_stdev({2.0, 2.0, 2.0}), 0.0);
}

TEST(ResearchEvalTest, WalkForwardUsesOnlyTrainForSelection) {
    const auto quotes = parse(
        "100,100,102,10,10\n"
        "200,90,92,10,10\n"
        "300,100,102,10,10\n"
        "400,90,92,10,10\n"
        "500,100,102,10,10\n"
        "600,90,92,10,10\n");
    StrategyConfig base{5, 1, 1};
    ExecutionConfig execution{0, 0, RiskLimits{10, 10, 1, 1000}};
    const auto report = walk_forward(
        quotes, {1, 5, 10}, base, execution, 0.5, "wf");
    EXPECT_FALSE(report.test.dataset_label.empty());
    EXPECT_GT(report.selected.entry_ticks, 0);
}

TEST(ResearchBacktestTest, BuyAndHoldBuysOnce) {
    const auto quotes = parse(
        "100,100,102,10,10\n"
        "200,110,112,10,10\n"
        "300,120,122,10,10\n");
    StrategyConfig strategy{5, 1, 1};
    ExecutionConfig execution{0, 0, RiskLimits{10, 10, 1, 1000}};
    const auto result =
        run_backtest(quotes, strategy, execution, "bh", true);
    EXPECT_EQ(result.actions[0], StrategyAction::Buy);
    EXPECT_EQ(result.actions[1], StrategyAction::Hold);
    EXPECT_EQ(result.position, 1);
}

}  // namespace
}  // namespace apexhft
