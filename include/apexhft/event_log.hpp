#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace apexhft {

enum class EventType {
    OrderAccepted,
    OrderRejected,
    OrderCancelled,
    TradeExecuted
};

struct AuditEvent {
    std::uint64_t sequence;
    EventType type;
    std::string details;
};

class EventLog {
public:
    void append(EventType type, std::string details) {
        if (next_sequence_ == 0) {
            throw std::overflow_error("Event sequence exhausted");
        }

        events_.push_back(
            AuditEvent{next_sequence_, type, std::move(details)});

        if (next_sequence_ == UINT64_MAX) {
            next_sequence_ = 0;
        } else {
            ++next_sequence_;
        }
    }

    [[nodiscard]] const std::vector<AuditEvent>& events() const noexcept {
        return events_;
    }

    [[nodiscard]] std::uint64_t size() const noexcept {
        return static_cast<std::uint64_t>(events_.size());
    }

private:
    std::vector<AuditEvent> events_;
    std::uint64_t next_sequence_{1};
};

}  // namespace apexhft
