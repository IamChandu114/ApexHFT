#pragma once

#include "apexhft/oms.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace apexhft {

struct QuoteObservation {
    std::uint64_t timestamp_ns{0};
    std::int64_t bid_ticks{0};
    std::int64_t ask_ticks{0};
    std::uint64_t bid_qty{0};
    std::uint64_t ask_qty{0};
};

enum class StrategyAction { Hold, Buy, Sell };

struct StrategyConfig {
    std::int64_t entry_ticks{5};
    std::uint64_t order_quantity{1};
    std::int64_t max_abs_position{1};
};

struct ExecutionConfig {
    std::int64_t fee_ticks_per_unit{1};
    std::int64_t slippage_ticks{0};
    RiskLimits risk{100, 100, 1, 1'000'000};
};

struct FillRecord {
    std::uint64_t timestamp_ns{0};
    Trade trade;
    bool owned_was_buy{false};
};

struct BacktestResult {
    std::int64_t position{0};
    std::int64_t cash_tick_lots{0};
    std::int64_t final_equity_tick_lots{0};
    std::int64_t gross_pnl_tick_lots{0};
    std::int64_t net_pnl_tick_lots{0};
    std::int64_t transaction_costs_tick_lots{0};
    std::uint64_t submitted_quantity{0};
    std::uint64_t filled_quantity{0};
    std::uint64_t rejected_orders{0};
    std::uint64_t cancelled_residuals{0};
    std::size_t trades{0};
    double fill_rate{0};
    double max_drawdown_tick_lots{0};
    double equity_volatility{0};
    std::vector<std::int64_t> equity_curve;
    std::vector<FillRecord> fills;
    std::vector<StrategyAction> actions;
    StrategyConfig strategy;
    ExecutionConfig execution;
    std::string dataset_label;
};

inline std::int64_t quote_mid_ticks(const QuoteObservation& quote) {
    return (quote.bid_ticks + quote.ask_ticks) / 2;
}

inline std::int64_t checked_mul(std::int64_t left, std::int64_t right) {
    std::int64_t result = 0;
    if (__builtin_mul_overflow(left, right, &result)) {
        throw std::overflow_error("Accounting multiply overflows");
    }
    return result;
}

namespace detail {

inline std::string trim(std::string_view view) {
    while (!view.empty() && (view.front() == ' ' || view.front() == '\t' ||
                             view.front() == '\r')) {
        view.remove_prefix(1);
    }
    while (!view.empty() && (view.back() == ' ' || view.back() == '\t' ||
                             view.back() == '\r')) {
        view.remove_suffix(1);
    }
    return std::string{view};
}

inline std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, ',')) {
        fields.push_back(trim(field));
    }
    if (!line.empty() && line.back() == ',') {
        fields.push_back({});
    }
    return fields;
}

inline std::uint64_t parse_u64(const std::string& text, const char* name,
                               std::size_t line) {
    if (text.empty()) {
        throw std::invalid_argument(
            "Missing " + std::string(name) + " at line " + std::to_string(line));
    }
    try {
        std::size_t idx = 0;
        const auto value = std::stoull(text, &idx, 10);
        if (idx != text.size()) {
            throw std::invalid_argument("trailing");
        }
        return value;
    } catch (const std::exception&) {
        throw std::invalid_argument(
            "Invalid " + std::string(name) + " at line " + std::to_string(line));
    }
}

inline std::int64_t parse_i64(const std::string& text, const char* name,
                              std::size_t line) {
    if (text.empty()) {
        throw std::invalid_argument(
            "Missing " + std::string(name) + " at line " + std::to_string(line));
    }
    try {
        std::size_t idx = 0;
        const auto value = std::stoll(text, &idx, 10);
        if (idx != text.size()) {
            throw std::invalid_argument("trailing");
        }
        return value;
    } catch (const std::exception&) {
        throw std::invalid_argument(
            "Invalid " + std::string(name) + " at line " + std::to_string(line));
    }
}

}  // namespace detail

// CSV schema (comments begin with #):
// timestamp_ns,bid_ticks,ask_ticks,bid_qty,ask_qty
// Policy: timestamps must be positive and non-decreasing. Equal timestamps
// keep file order. Unsorted input is rejected, never silently sorted.
// Bid must be strictly below ask. Quantities must be positive.
inline std::vector<QuoteObservation> load_quotes(std::istream& in) {
    std::vector<QuoteObservation> quotes;
    std::string line;
    std::size_t line_number = 0;
    std::optional<std::uint64_t> previous_ts;

    while (std::getline(in, line)) {
        ++line_number;
        const auto trimmed = detail::trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }

        const auto fields = detail::split_csv(trimmed);
        if (fields.size() != 5) {
            throw std::invalid_argument(
                "Expected 5 fields at line " + std::to_string(line_number));
        }

        QuoteObservation quote;
        quote.timestamp_ns =
            detail::parse_u64(fields[0], "timestamp_ns", line_number);
        quote.bid_ticks = detail::parse_i64(fields[1], "bid_ticks", line_number);
        quote.ask_ticks = detail::parse_i64(fields[2], "ask_ticks", line_number);
        quote.bid_qty = detail::parse_u64(fields[3], "bid_qty", line_number);
        quote.ask_qty = detail::parse_u64(fields[4], "ask_qty", line_number);

        if (quote.timestamp_ns == 0) {
            throw std::invalid_argument(
                "Timestamp must be positive at line " +
                std::to_string(line_number));
        }
        if (quote.bid_ticks <= 0 || quote.ask_ticks <= 0) {
            throw std::invalid_argument(
                "Prices must be positive at line " +
                std::to_string(line_number));
        }
        if (quote.bid_ticks >= quote.ask_ticks) {
            throw std::invalid_argument(
                "Bid must be strictly below ask at line " +
                std::to_string(line_number));
        }
        if (quote.bid_qty == 0 || quote.ask_qty == 0) {
            throw std::invalid_argument(
                "Quantities must be positive at line " +
                std::to_string(line_number));
        }
        if (previous_ts && quote.timestamp_ns < *previous_ts) {
            throw std::invalid_argument(
                "Quotes are not sorted by timestamp at line " +
                std::to_string(line_number));
        }
        previous_ts = quote.timestamp_ns;
        quotes.push_back(quote);
    }

    return quotes;
}

inline std::vector<QuoteObservation> load_quotes_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::invalid_argument("Cannot open quote file: " + path);
    }
    return load_quotes(in);
}

inline StrategyAction threshold_reversion_action(
    const StrategyConfig& config,
    std::optional<std::int64_t> previous_mid,
    const QuoteObservation& quote,
    std::int64_t position) {
    if (!previous_mid) {
        return StrategyAction::Hold;
    }
    if (config.entry_ticks <= 0 || config.order_quantity == 0 ||
        config.max_abs_position < 0) {
        throw std::invalid_argument("Invalid strategy config");
    }

    const auto mid = quote_mid_ticks(quote);
    const auto qty = static_cast<std::int64_t>(config.order_quantity);

    if (mid <= *previous_mid - config.entry_ticks) {
        if (position + qty <= config.max_abs_position) {
            return StrategyAction::Buy;
        }
        return StrategyAction::Hold;
    }
    if (mid >= *previous_mid + config.entry_ticks) {
        if (position - qty >= -config.max_abs_position && position > 0) {
            return StrategyAction::Sell;
        }
        return StrategyAction::Hold;
    }
    return StrategyAction::Hold;
}

inline double sample_stdev(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }
    double mean = 0;
    for (double value : values) {
        mean += value;
    }
    mean /= static_cast<double>(values.size());
    double acc = 0;
    for (double value : values) {
        const double d = value - mean;
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(values.size() - 1));
}

inline double max_drawdown(const std::vector<std::int64_t>& equity) {
    if (equity.empty()) {
        return 0.0;
    }
    std::int64_t peak = equity.front();
    std::int64_t worst = 0;
    for (auto value : equity) {
        if (value > peak) {
            peak = value;
        }
        const auto dd = peak - value;
        if (dd > worst) {
            worst = dd;
        }
    }
    return static_cast<double>(worst);
}

inline BacktestResult run_backtest(
    const std::vector<QuoteObservation>& quotes,
    StrategyConfig strategy,
    ExecutionConfig execution,
    std::string dataset_label,
    bool buy_and_hold) {
    BacktestResult result;
    result.strategy = strategy;
    result.execution = execution;
    result.dataset_label = std::move(dataset_label);

    if (strategy.entry_ticks <= 0 || strategy.order_quantity == 0 ||
        strategy.max_abs_position < 0 ||
        execution.fee_ticks_per_unit < 0 || execution.slippage_ticks < 0) {
        throw std::invalid_argument("Invalid backtest configuration");
    }

    OrderManagementSystem oms{execution.risk};
    std::uint64_t next_id = 1;
    std::optional<std::int64_t> previous_mid;
    bool bought_once = false;

    auto cancel_active = [&] {
        const auto ids = oms.owned_order_ids();
        for (const auto id : ids) {
            const auto& order = oms.get_order(id);
            if (order.status() == OrderStatus::New ||
                order.status() == OrderStatus::PartiallyFilled) {
                oms.cancel(id);
                ++result.cancelled_residuals;
            }
        }
    };

    auto apply_owned_trades = [&](std::uint64_t ts, bool owned_buy,
                                  const std::vector<Trade>& trades) {
        for (const auto& trade : trades) {
            const auto qty = static_cast<std::int64_t>(trade.quantity.value());
            const auto notional = checked_mul(trade.price.value(), qty);
            const auto fee = checked_mul(execution.fee_ticks_per_unit, qty);
            if (owned_buy) {
                result.cash_tick_lots -= notional;
                result.cash_tick_lots -= fee;
            } else {
                result.cash_tick_lots += notional;
                result.cash_tick_lots -= fee;
            }
            result.transaction_costs_tick_lots += fee;
            result.filled_quantity += trade.quantity.value();
            result.fills.push_back(FillRecord{ts, trade, owned_buy});
            ++result.trades;
            result.gross_pnl_tick_lots =
                owned_buy ? result.gross_pnl_tick_lots - notional
                          : result.gross_pnl_tick_lots + notional;
        }
    };

    auto take = [&](const QuoteObservation& quote, Side owned_side) {
        const auto qty = strategy.order_quantity;
        std::int64_t limit = 0;
        std::uint64_t available = 0;
        if (owned_side == Side::Buy) {
            limit = quote.ask_ticks + execution.slippage_ticks;
            available = quote.ask_qty;
        } else {
            limit = quote.bid_ticks - execution.slippage_ticks;
            available = quote.bid_qty;
        }
        if (limit <= 0) {
            ++result.rejected_orders;
            return;
        }

        const auto ext_qty = std::min(qty, available);
        const auto ext_side =
            owned_side == Side::Buy ? Side::Sell : Side::Buy;
        (void)oms.add_external_liquidity(Order{
            OrderId{next_id++}, ext_side, OrderType::Limit,
            Quantity{ext_qty}, PriceTicks{limit}});

        result.submitted_quantity += qty;
        auto submission = oms.submit(Order{
            OrderId{next_id++}, owned_side, OrderType::Limit,
            Quantity{qty}, PriceTicks{limit}});
        if (!submission.accepted) {
            ++result.rejected_orders;
            cancel_active();
            return;
        }
        apply_owned_trades(quote.timestamp_ns, owned_side == Side::Buy,
                           submission.trades);
        cancel_active();
    };

    for (const auto& quote : quotes) {
        cancel_active();
        StrategyAction action = StrategyAction::Hold;
        if (buy_and_hold) {
            if (!bought_once) {
                action = StrategyAction::Buy;
                bought_once = true;
            }
        } else {
            action = threshold_reversion_action(
                strategy, previous_mid, quote, oms.position());
        }

        result.actions.push_back(action);
        if (action == StrategyAction::Buy) {
            take(quote, Side::Buy);
        } else if (action == StrategyAction::Sell) {
            take(quote, Side::Sell);
        }

        previous_mid = quote_mid_ticks(quote);
        const auto mark = quote_mid_ticks(quote);
        const auto equity =
            result.cash_tick_lots + checked_mul(oms.position(), mark);
        result.equity_curve.push_back(equity);
    }

    result.position = oms.position();
    if (!quotes.empty()) {
        result.final_equity_tick_lots = result.equity_curve.back();
    }
    result.net_pnl_tick_lots = result.final_equity_tick_lots;
    result.fill_rate = result.submitted_quantity == 0
        ? 0.0
        : static_cast<double>(result.filled_quantity) /
              static_cast<double>(result.submitted_quantity);
    result.max_drawdown_tick_lots = max_drawdown(result.equity_curve);

    std::vector<double> diffs;
    for (std::size_t i = 1; i < result.equity_curve.size(); ++i) {
        diffs.push_back(static_cast<double>(
            result.equity_curve[i] - result.equity_curve[i - 1]));
    }
    result.equity_volatility = sample_stdev(diffs);
    return result;
}

struct WalkForwardReport {
    StrategyConfig selected;
    BacktestResult train;
    BacktestResult test;
    BacktestResult buy_hold_test;
};

inline WalkForwardReport walk_forward(
    const std::vector<QuoteObservation>& quotes,
    const std::vector<std::int64_t>& entry_grid,
    StrategyConfig base,
    ExecutionConfig execution,
    double train_fraction,
    std::string dataset_label) {
    if (quotes.size() < 4) {
        throw std::invalid_argument(
            "Walk-forward requires at least 4 observations");
    }
    if (train_fraction <= 0.0 || train_fraction >= 1.0) {
        throw std::invalid_argument("train_fraction must be in (0, 1)");
    }
    if (entry_grid.empty()) {
        throw std::invalid_argument("entry_grid must not be empty");
    }

    const auto train_n = std::max<std::size_t>(
        2, static_cast<std::size_t>(
               train_fraction * static_cast<double>(quotes.size())));
    const std::vector<QuoteObservation> train_data(
        quotes.begin(), quotes.begin() + static_cast<std::ptrdiff_t>(train_n));
    const std::vector<QuoteObservation> test_data(
        quotes.begin() + static_cast<std::ptrdiff_t>(train_n), quotes.end());
    if (test_data.empty()) {
        throw std::invalid_argument("Walk-forward test period is empty");
    }

    WalkForwardReport report;
    bool have_best = false;
    std::int64_t best_pnl = 0;

    for (auto entry : entry_grid) {
        auto cfg = base;
        cfg.entry_ticks = entry;
        auto trained = run_backtest(
            train_data, cfg, execution, dataset_label + ":train", false);
        if (!have_best || trained.net_pnl_tick_lots > best_pnl) {
            have_best = true;
            best_pnl = trained.net_pnl_tick_lots;
            report.selected = cfg;
            report.train = std::move(trained);
        }
    }

    report.test = run_backtest(
        test_data, report.selected, execution, dataset_label + ":test", false);
    report.buy_hold_test = run_backtest(
        test_data, report.selected, execution,
        dataset_label + ":buy_hold_test", true);
    return report;
}

}  // namespace apexhft
