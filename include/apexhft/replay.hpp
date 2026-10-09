#pragma once

#include "apexhft/matching_engine.hpp"

#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace apexhft {

struct SubmitEvent {
    Order order;
};

struct CancelEvent {
    OrderId order_id;
};

using MarketEvent = std::variant<SubmitEvent, CancelEvent>;

struct SequencedEvent {
    std::uint64_t sequence;
    MarketEvent event;
};

struct ReplayResult {
    std::vector<Trade> trades;
    std::uint64_t events_processed{0};
};

class MarketReplay {
public:
    [[nodiscard]] ReplayResult run(
        const std::vector<SequencedEvent>& events) const {
        MatchingEngine engine;
        ReplayResult result;
        std::uint64_t expected_sequence = 1;

        for (const auto& item : events) {
            if (item.sequence != expected_sequence) {
                throw std::invalid_argument(
                    "Replay event sequence must start at 1 and be contiguous");
            }

            std::visit(
                [&engine, &result](const auto& event) {
                    using Event = std::decay_t<decltype(event)>;

                    if constexpr (std::is_same_v<Event, SubmitEvent>) {
                        auto trades = engine.submit(event.order);
                        result.trades.insert(
                            result.trades.end(),
                            trades.begin(), trades.end());
                    } else {
                        engine.cancel(event.order_id);
                    }
                },
                item.event);

            ++expected_sequence;
            ++result.events_processed;
        }

        return result;
    }
};

}  // namespace apexhft
