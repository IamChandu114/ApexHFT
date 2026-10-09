#pragma once

#include "apexhft/oms.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace apexhft {

// Input operations required to reconstruct OMS state. Audit events are derived
// output from these commands; they are not a second source of truth.
enum class OmsReplayOp {
    SubmitOwned,
    CancelOwned,
    AddExternalLiquidity
};

struct OmsReplayCommand {
    std::uint64_t sequence{0};
    OmsReplayOp op{OmsReplayOp::SubmitOwned};
    std::optional<Order> order;
    std::optional<OrderId> cancel_id;
};

struct RestingOrderSnapshot {
    OrderId id;
    Side side;
    std::uint64_t remaining_quantity{0};
    std::int64_t limit_price_ticks{0};
    OrderStatus status{OrderStatus::New};
};

struct OmsCommandRecord {
    std::uint64_t sequence{0};
    OmsReplayOp op{OmsReplayOp::SubmitOwned};
    bool accepted{true};
    std::string reason;
    std::vector<Trade> trades;
};

struct OmsReplayResult {
    std::int64_t position{0};
    std::size_t active_owned_orders{0};
    std::uint64_t commands_processed{0};
    std::vector<Trade> trades;
    std::vector<AuditEvent> audit_events;
    std::vector<OmsCommandRecord> commands;
    std::vector<RestingOrderSnapshot> remaining_owned_orders;
};

inline RestingOrderSnapshot snapshot_order(const Order& order) {
    return RestingOrderSnapshot{
        order.id(),
        order.side(),
        order.remaining_quantity(),
        order.limit_price() ? order.limit_price()->value() : 0,
        order.status()
    };
}

class OmsReplay {
public:
    explicit OmsReplay(RiskLimits limits) : limits_(limits) {}

    // Constructs a fresh OMS from the configured risk limits and applies
    // commands in sequence. Invalid sequences abort the run with
    // std::invalid_argument and do not return a result.
    //
    // SubmitOwned rejections (risk, self-cross, unsupported type) are recorded
    // and do not abort replay. Engine exceptions (duplicate IDs) and cancel
    // failures (unknown, inactive, or non-owned) propagate, matching direct OMS
    // execution. Prior commands remain applied on the OMS instance that throws;
    // the caller does not receive a partial OmsReplayResult.
    //
    // Exception safety matches OrderManagementSystem: rejections before matching
    // leave book and position unchanged. After matching-engine submit succeeds,
    // a later audit or position failure is not rolled back.
    [[nodiscard]] OmsReplayResult run(
        const std::vector<OmsReplayCommand>& commands) const {
        OrderManagementSystem oms{limits_};
        OmsReplayResult result;
        std::uint64_t expected_sequence = 1;

        for (const auto& command : commands) {
            if (command.sequence != expected_sequence) {
                throw std::invalid_argument(
                    "OMS replay command sequence must start at 1 and be contiguous");
            }

            OmsCommandRecord record;
            record.sequence = command.sequence;
            record.op = command.op;

            switch (command.op) {
            case OmsReplayOp::SubmitOwned: {
                if (!command.order.has_value()) {
                    throw std::invalid_argument(
                        "SubmitOwned requires an order");
                }
                auto submission = oms.submit(*command.order);
                record.accepted = submission.accepted;
                record.reason = std::move(submission.reason);
                record.trades = std::move(submission.trades);
                break;
            }
            case OmsReplayOp::CancelOwned: {
                if (!command.cancel_id.has_value()) {
                    throw std::invalid_argument(
                        "CancelOwned requires cancel_id");
                }
                oms.cancel(*command.cancel_id);
                record.accepted = true;
                break;
            }
            case OmsReplayOp::AddExternalLiquidity: {
                if (!command.order.has_value()) {
                    throw std::invalid_argument(
                        "AddExternalLiquidity requires an order");
                }
                record.trades = oms.add_external_liquidity(*command.order);
                record.accepted = true;
                break;
            }
            }

            result.trades.insert(
                result.trades.end(),
                record.trades.begin(),
                record.trades.end());
            result.commands.push_back(std::move(record));
            ++expected_sequence;
            ++result.commands_processed;
        }

        result.position = oms.position();
        result.active_owned_orders = oms.active_order_count();
        result.audit_events = oms.event_log().events();

        for (const auto id : oms.owned_order_ids()) {
            const auto& order = oms.get_order(id);
            if (order.status() == OrderStatus::New ||
                order.status() == OrderStatus::PartiallyFilled) {
                result.remaining_owned_orders.push_back(snapshot_order(order));
            }
        }

        return result;
    }

private:
    RiskLimits limits_;
};

}  // namespace apexhft
