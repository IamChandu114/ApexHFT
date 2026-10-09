#include "apexhft/matching_engine.hpp"
#include "apexhft/oms_replay.hpp"
#include "apexhft/replay.hpp"
#include "apexhft/version.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace apexhft;
using Clock = std::chrono::steady_clock;

#ifndef APEXHFT_BENCH_BUILD_TYPE
#define APEXHFT_BENCH_BUILD_TYPE "unknown"
#endif

struct Rng {
    std::uint64_t state;

    explicit Rng(std::uint64_t seed) : state(seed) {}

    std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1ULL;
        return state;
    }

    std::uint64_t bounded(std::uint64_t n) {
        return n == 0 ? 0 : next() % n;
    }
};

struct LatencyStats {
    double p50_ns{0};
    double p95_ns{0};
    double p99_ns{0};
    bool available{false};
};

LatencyStats percentiles(std::vector<double> samples) {
    LatencyStats stats;
    if (samples.empty()) {
        return stats;
    }
    std::sort(samples.begin(), samples.end());
    auto at = [&](double q) {
        const auto idx = static_cast<std::size_t>(
            q * static_cast<double>(samples.size() - 1));
        return samples[idx];
    };
    stats.p50_ns = at(0.50);
    stats.p95_ns = at(0.95);
    stats.p99_ns = at(0.99);
    stats.available = true;
    return stats;
}

struct RunResult {
    std::string name;
    std::size_t operations{0};
    std::size_t setup_operations{0};
    int repetitions{0};
    int warmup{0};
    double median_ms{0};
    double min_ms{0};
    double ops_per_sec{0};
    LatencyStats latency;
    std::string notes;
};

Order make_limit(std::uint64_t id, Side side, std::uint64_t qty,
                 std::int64_t price) {
    return Order{OrderId{id}, side, OrderType::Limit, Quantity{qty},
                 PriceTicks{price}};
}

template <typename Fn>
double time_ms(Fn&& fn) {
    const auto start = Clock::now();
    fn();
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template <typename Setup, typename Body>
RunResult measure(const std::string& name, std::size_t operations,
                  std::size_t setup_operations, int warmup, int reps,
                  Setup&& setup, Body&& body, const std::string& notes,
                  bool capture_latency) {
    std::vector<double> elapsed;
    std::vector<double> sample_latencies;
    elapsed.reserve(static_cast<std::size_t>(reps));

    for (int i = 0; i < warmup; ++i) {
        setup();
        body(nullptr);
    }

    for (int i = 0; i < reps; ++i) {
        setup();
        std::vector<double>* sink =
            capture_latency && i == 0 ? &sample_latencies : nullptr;
        elapsed.push_back(time_ms([&] { body(sink); }));
    }

    std::sort(elapsed.begin(), elapsed.end());
    RunResult result;
    result.name = name;
    result.operations = operations;
    result.setup_operations = setup_operations;
    result.repetitions = reps;
    result.warmup = warmup;
    result.min_ms = elapsed.front();
    result.median_ms = elapsed[elapsed.size() / 2];
    result.ops_per_sec =
        result.median_ms > 0.0
            ? (static_cast<double>(operations) * 1000.0) / result.median_ms
            : 0.0;
    result.latency = percentiles(std::move(sample_latencies));
    result.notes = notes;
    return result;
}

void record_op(std::vector<double>* latencies, Clock::time_point start) {
    if (latencies == nullptr) {
        return;
    }
    const auto ns = std::chrono::duration<double, std::nano>(
                        Clock::now() - start)
                        .count();
    latencies->push_back(ns);
}

std::string read_cpu_model() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto pos = line.find(':');
            if (pos != std::string::npos) {
                auto value = line.substr(pos + 1);
                while (!value.empty() && value.front() == ' ') {
                    value.erase(value.begin());
                }
                return value;
            }
        }
    }
    return "unknown";
}

std::string read_os() {
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            auto value = line.substr(12);
            if (!value.empty() && value.front() == '"') {
                value.erase(value.begin());
            }
            if (!value.empty() && value.back() == '"') {
                value.pop_back();
            }
            return value;
        }
    }
    return "unknown";
}

void print_result(const RunResult& result) {
    std::cout << "workload=" << result.name
              << " ops=" << result.operations
              << " setup_ops=" << result.setup_operations
              << " warmup=" << result.warmup
              << " reps=" << result.repetitions
              << " min_ms=" << std::fixed << std::setprecision(3)
              << result.min_ms
              << " median_ms=" << result.median_ms
              << " ops_per_sec=" << std::setprecision(1) << result.ops_per_sec;
    if (result.latency.available) {
        std::cout << " p50_ns=" << std::setprecision(0) << result.latency.p50_ns
                  << " p95_ns=" << result.latency.p95_ns
                  << " p99_ns=" << result.latency.p99_ns;
    }
    std::cout << " notes=\"" << result.notes << "\"\n";
}

}  // namespace

int main() {
    constexpr int kWarmup = 1;
    constexpr int kReps = 5;
    constexpr std::uint64_t kSeed = 20261009ULL;

    std::cout << "ApexHFT benchmark " << apexhft::version() << '\n';
    std::cout << "cpu=" << read_cpu_model() << '\n';
    std::cout << "os=" << read_os() << '\n';
    std::cout << "compiler=" <<
#ifdef __clang__
        "clang " << __clang_major__ << "." << __clang_minor__
#else
        "gcc " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__
#endif
              << '\n';
    std::cout << "build_type=" << APEXHFT_BENCH_BUILD_TYPE << '\n';
    std::cout << "cxx_standard=20\n";
    std::cout << "sanitizers="
#ifdef __SANITIZE_ADDRESS__
              << "address,undefined"
#else
              << "off"
#endif
              << '\n';
    std::cout << "seed=" << kSeed << '\n';
    std::cout << "clock=std::chrono::steady_clock\n";
    std::cout << "timing=setup excluded; per-op latency from first timed rep\n";
    std::cout << "limitation=per-op percentiles include clock overhead and "
                 "are noisy below ~100ns\n";

    std::vector<RunResult> results;

    auto insert_non_crossing = [&](std::size_t book_size, std::size_t inserts) {
        std::vector<Order> setup_orders;
        std::vector<Order> timed_orders;
        setup_orders.reserve(book_size);
        timed_orders.reserve(inserts);
        for (std::size_t i = 0; i < book_size; ++i) {
            const auto side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            const auto price = (side == Side::Buy) ? 90 : 110;
            setup_orders.push_back(
                make_limit(i + 1, side, 1, price));
        }
        for (std::size_t i = 0; i < inserts; ++i) {
            const auto side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            const auto price = (side == Side::Buy) ? 89 : 111;
            timed_orders.push_back(
                make_limit(book_size + i + 1, side, 1, price));
        }

        MatchingEngine engine;
        return measure(
            "insert_non_crossing_book" + std::to_string(book_size),
            inserts, book_size, kWarmup, kReps,
            [&] {
                engine = MatchingEngine{};
                for (const auto& order : setup_orders) {
                    (void)engine.submit(order);
                }
            },
            [&](std::vector<double>* latencies) {
                for (const auto& order : timed_orders) {
                    const auto start = Clock::now();
                    (void)engine.submit(order);
                    record_op(latencies, start);
                }
            },
            "non-crossing limit inserts at 89/111 after book at 90/110",
            true);
    };

    results.push_back(measure(
        "insert_empty", 10'000, 0, kWarmup, kReps,
        [] {},
        [&](std::vector<double>* latencies) {
            MatchingEngine engine;
            for (std::uint64_t i = 1; i <= 10'000; ++i) {
                const auto side = (i % 2 == 0) ? Side::Buy : Side::Sell;
                const auto price = (side == Side::Buy) ? 90 : 110;
                const auto start = Clock::now();
                (void)engine.submit(make_limit(i, side, 1, price));
                record_op(latencies, start);
            }
        },
        "10000 non-crossing inserts into an empty book",
        true));

    results.push_back(insert_non_crossing(1'000, 5'000));
    results.push_back(insert_non_crossing(10'000, 5'000));

    MatchingEngine marketable_engine;
    results.push_back(measure(
        "marketable_trades", 10'000, 10'000, kWarmup, kReps,
        [&] {
            marketable_engine = MatchingEngine{};
            for (std::uint64_t i = 1; i <= 10'000; ++i) {
                (void)marketable_engine.submit(make_limit(i, Side::Sell, 1, 100));
            }
        },
        [&](std::vector<double>* latencies) {
            for (std::uint64_t i = 1; i <= 10'000; ++i) {
                const auto start = Clock::now();
                (void)marketable_engine.submit(
                    make_limit(10'000 + i, Side::Buy, 1, 100));
                record_op(latencies, start);
            }
        },
        "10000 resting sells then 10000 marketable buys; timed buys only",
        true));

    MatchingEngine cancel_engine;
    results.push_back(measure(
        "cancel_existing", 5'000, 5'000, kWarmup, kReps,
        [&] {
            cancel_engine = MatchingEngine{};
            for (std::uint64_t i = 1; i <= 5'000; ++i) {
                const auto side = (i % 2 == 0) ? Side::Buy : Side::Sell;
                (void)cancel_engine.submit(
                    make_limit(i, side, 1, side == Side::Buy ? 90 : 110));
            }
        },
        [&](std::vector<double>* latencies) {
            for (std::uint64_t i = 1; i <= 5'000; ++i) {
                const auto start = Clock::now();
                cancel_engine.cancel(OrderId{i});
                record_op(latencies, start);
            }
        },
        "cancel 5000 previously inserted resting orders",
        true));

    auto prune_live = [](MatchingEngine& engine,
                         std::vector<std::uint64_t>& live) {
        live.erase(std::remove_if(live.begin(), live.end(),
                       [&](std::uint64_t id) {
                           const auto status =
                               engine.get_order(OrderId{id}).status();
                           return status != OrderStatus::New &&
                                  status != OrderStatus::PartiallyFilled;
                       }),
            live.end());
    };

    results.push_back(measure(
        "mixed_deterministic", 20'000, 0, kWarmup, kReps,
        [] {},
        [&](std::vector<double>* latencies) {
            MatchingEngine engine;
            Rng rng{kSeed};
            std::vector<std::uint64_t> live;
            live.reserve(20'000);
            std::uint64_t next_id = 1;
            for (int i = 0; i < 20'000; ++i) {
                prune_live(engine, live);
                const auto op = rng.bounded(10);
                const auto start = Clock::now();
                if (op < 7 || live.empty()) {
                    const auto side =
                        rng.bounded(2) == 0 ? Side::Buy : Side::Sell;
                    const auto price =
                        static_cast<std::int64_t>(90 + rng.bounded(21));
                    (void)engine.submit(
                        make_limit(next_id, side, 1, price));
                    live.push_back(next_id);
                    ++next_id;
                } else {
                    const auto idx = rng.bounded(live.size());
                    engine.cancel(OrderId{live[idx]});
                    live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
                }
                record_op(latencies, start);
            }
        },
        "seeded mix: 70% insert / 30% cancel of still-active orders",
        true));

    std::vector<SequencedEvent> replay_events;
    replay_events.reserve(20'000);
    {
        MatchingEngine shadow;
        Rng rng{kSeed};
        std::vector<std::uint64_t> live;
        std::uint64_t next_id = 1;
        for (std::uint64_t seq = 1; seq <= 20'000; ++seq) {
            prune_live(shadow, live);
            const auto op = rng.bounded(10);
            if (op < 8 || live.empty()) {
                const auto side = rng.bounded(2) == 0 ? Side::Buy : Side::Sell;
                const auto price =
                    static_cast<std::int64_t>(90 + rng.bounded(21));
                const auto order = make_limit(next_id, side, 1, price);
                replay_events.push_back(
                    SequencedEvent{seq, SubmitEvent{order}});
                (void)shadow.submit(order);
                live.push_back(next_id);
                ++next_id;
            } else {
                const auto idx = rng.bounded(live.size());
                replay_events.push_back(
                    SequencedEvent{seq, CancelEvent{OrderId{live[idx]}}});
                shadow.cancel(OrderId{live[idx]});
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
            }
        }
    }

    results.push_back(measure(
        "market_replay", replay_events.size(), 0, kWarmup, kReps,
        [] {},
        [&](std::vector<double>* latencies) {
            const auto start = Clock::now();
            (void)MarketReplay{}.run(replay_events);
            record_op(latencies, start);
        },
        "20000 sequenced market events; batch timing only",
        false));

    std::vector<OmsReplayCommand> oms_commands;
    oms_commands.reserve(8'000);
    {
        std::uint64_t seq = 1;
        std::uint64_t next_owned = 1;
        std::uint64_t next_ext = 1'000'000;
        for (int i = 0; i < 2000; ++i) {
            oms_commands.push_back(OmsReplayCommand{
                seq++, OmsReplayOp::AddExternalLiquidity,
                make_limit(next_ext++, Side::Sell, 1, 100), std::nullopt});
            oms_commands.push_back(OmsReplayCommand{
                seq++, OmsReplayOp::SubmitOwned,
                make_limit(next_owned++, Side::Buy, 1, 100), std::nullopt});
        }
        for (int i = 0; i < 2000; ++i) {
            oms_commands.push_back(OmsReplayCommand{
                seq++, OmsReplayOp::SubmitOwned,
                make_limit(next_owned, Side::Buy, 1, 90), std::nullopt});
            oms_commands.push_back(OmsReplayCommand{
                seq++, OmsReplayOp::CancelOwned, std::nullopt,
                OrderId{next_owned}});
            ++next_owned;
        }
    }

    const RiskLimits bench_limits{1'000, 1'000'000, 1, 1'000'000};
    results.push_back(measure(
        "oms_replay", oms_commands.size(), 0, kWarmup, kReps,
        [] {},
        [&](std::vector<double>* latencies) {
            const auto start = Clock::now();
            (void)OmsReplay{bench_limits}.run(oms_commands);
            record_op(latencies, start);
        },
        "8000 OMS commands: 2000 external+owned fills plus 2000 rest+cancel",
        false));

    for (const auto& result : results) {
        print_result(result);
    }

    return 0;
}
