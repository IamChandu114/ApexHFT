#include "apexhft/research.hpp"
#include "apexhft/version.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void print_metrics(const char* title, const apexhft::BacktestResult& result) {
    std::cout << title << '\n';
    std::cout << "  dataset=" << result.dataset_label << '\n';
    std::cout << "  entry_ticks=" << result.strategy.entry_ticks
              << " order_qty=" << result.strategy.order_quantity
              << " max_abs_position=" << result.strategy.max_abs_position
              << '\n';
    std::cout << "  fee_ticks_per_unit=" << result.execution.fee_ticks_per_unit
              << " slippage_ticks=" << result.execution.slippage_ticks << '\n';
    std::cout << "  position=" << result.position
              << " cash_tick_lots=" << result.cash_tick_lots
              << " equity_tick_lots=" << result.final_equity_tick_lots << '\n';
    std::cout << "  gross_pnl_tick_lots=" << result.gross_pnl_tick_lots
              << " net_pnl_tick_lots=" << result.net_pnl_tick_lots
              << " costs_tick_lots=" << result.transaction_costs_tick_lots
              << '\n';
    std::cout << "  trades=" << result.trades
              << " filled_qty=" << result.filled_quantity
              << " submitted_qty=" << result.submitted_quantity
              << " fill_rate=" << result.fill_rate << '\n';
    std::cout << "  rejected=" << result.rejected_orders
              << " cancelled_residuals=" << result.cancelled_residuals
              << " max_drawdown_tick_lots=" << result.max_drawdown_tick_lots
              << " equity_vol=" << result.equity_volatility << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    std::string path = "data/synthetic_quotes.csv";
    apexhft::StrategyConfig strategy;
    apexhft::ExecutionConfig execution;
    double train_fraction = 0.5;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(
                    std::string("Missing value for ") + name);
            }
            return argv[++i];
        };
        if (arg == "--csv") {
            path = need("--csv");
        } else if (arg == "--entry-ticks") {
            strategy.entry_ticks = std::stoll(need("--entry-ticks"));
        } else if (arg == "--quantity") {
            strategy.order_quantity = std::stoull(need("--quantity"));
        } else if (arg == "--fee") {
            execution.fee_ticks_per_unit = std::stoll(need("--fee"));
        } else if (arg == "--slippage") {
            execution.slippage_ticks = std::stoll(need("--slippage"));
        } else if (arg == "--train-fraction") {
            train_fraction = std::stod(need("--train-fraction"));
        } else if (arg == "--help") {
            std::cout << "ApexHFT research " << apexhft::version() << '\n'
                      << "Usage: apexhft_research [--csv path] "
                         "[--entry-ticks N] [--quantity Q] [--fee F] "
                         "[--slippage S] [--train-fraction F]\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }

    std::cout << "ApexHFT research " << apexhft::version() << '\n';
    std::cout << "csv=" << path << '\n';
    std::cout << "provenance=synthetic_unless_path_says_otherwise\n";
    std::cout << "limitation=top-of-book snapshots cannot reconstruct queue "
                 "priority or market impact\n";

    const auto quotes = apexhft::load_quotes_file(path);
    std::cout << "observations=" << quotes.size() << '\n';

    if (quotes.empty()) {
        std::cout << "Empty dataset; no simulation performed.\n";
        return 0;
    }

    const auto full = apexhft::run_backtest(
        quotes, strategy, execution, path, false);
    const auto hold = apexhft::run_backtest(
        quotes, strategy, execution, path + ":buy_hold", true);
    print_metrics("threshold_reversion", full);
    print_metrics("buy_and_hold", hold);

    if (quotes.size() >= 4) {
        const auto wf = apexhft::walk_forward(
            quotes, {1, 2, 5, 10}, strategy, execution, train_fraction, path);
        std::cout << "walk_forward selected_entry_ticks="
                  << wf.selected.entry_ticks << '\n';
        print_metrics("walk_forward_train", wf.train);
        print_metrics("walk_forward_test", wf.test);
        print_metrics("walk_forward_buy_hold_test", wf.buy_hold_test);
    }

    return 0;
}
