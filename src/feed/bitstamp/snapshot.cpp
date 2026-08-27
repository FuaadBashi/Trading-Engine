#include <string_view>
#include <te/feed/bitstamp/snapshot.hpp>
#include <unordered_set>

#include "simdjson/ondemand.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/padded_string_view.h"
#include "te/core/text_to_int.hpp"

namespace te::bitstamp {

// On Demand is forward-only, so consume each row's three fields in one pass.
static Result<BookSnapshot, SnapshotError> readRowFields(simdjson::ondemand::value row_result,
                                                         std::string_view out[3]) {
    simdjson::ondemand::array row;
    if (row_result.get_array().get(row)) {
        return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::wrong_field_count);
    }
    int field_index = 0;
    for (auto elem_result : row) {
        std::string_view s;
        if (elem_result.get_string().get(s)) {
            return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::wrong_field_count);
        }
        if (field_index < 3) {
            out[field_index] = s;
        }
        ++field_index;
    }
    if (field_index != 3) {
        return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::wrong_field_count);
    }
    return Result<BookSnapshot, SnapshotError>::success(BookSnapshot{});
}

// Keep every Result alive while using valueIf(); calling valueIf() on a temporary dangles.
static Result<SnapshotOrder, SnapshotError> parseRow(const std::string_view fields[3],
                                                     InstrumentSpec spec, Side side) {
    SnapshotOrder order;
    order.side = side;

    const auto price_result = parseDecimal(fields[0], spec.price_decimals);
    const std::int64_t* price = price_result.valueIf();
    if (price == nullptr) {
        return Result<SnapshotOrder, SnapshotError>::failure(SnapshotError::invalid_price);
    }
    order.price = Price{*price};

    const auto qty_result = parseDecimal(fields[1], spec.quantity_decimals);
    const std::int64_t* qty = qty_result.valueIf();
    if (qty == nullptr) {
        return Result<SnapshotOrder, SnapshotError>::failure(SnapshotError::invalid_quantity);
    }
    order.quantity = Qty{*qty};

    const auto id_result = parseInteger(fields[2]);
    const std::uint64_t* id = id_result.valueIf();
    if (id == nullptr) {
        return Result<SnapshotOrder, SnapshotError>::failure(SnapshotError::invalid_order_id);
    }
    order.order_id = OrderId{*id};

    return Result<SnapshotOrder, SnapshotError>::success(order);
}

Result<BookSnapshot, SnapshotError> parseSnapshot(std::string_view text, InstrumentSpec spec) {
    BookSnapshot book;
    simdjson::ondemand::parser parser;
    simdjson::padded_string buffer = simdjson::padded_string(text);

    simdjson::ondemand::document doc;
    simdjson::error_code err = parser.iterate(buffer).get(doc);

    if (err) {
        return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::malformed_json);
    }

    std::string_view microtimestamp;
    if (doc["microtimestamp"].get_string().get(microtimestamp)) {
        return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::missing_microtimestamp);
    }
    const auto microtimestamp_result = parseInteger(microtimestamp);
    const std::uint64_t* microtimestamp_value = microtimestamp_result.valueIf();
    if (microtimestamp_value == nullptr) {
        return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::missing_microtimestamp);
    }
    book.microtimestamp = *microtimestamp_value;

    // Duplicate IDs make the seed ambiguous, so reject the entire snapshot.
    std::unordered_set<OrderId, OrderIdHash> seen_ids;

    struct SideSource {
        const char* key;
        Side side;
        SnapshotError missing_error;
    };
    const SideSource sources[2] = {
        {"bids", Side::buy, SnapshotError::missing_bids},
        {"asks", Side::sell, SnapshotError::missing_asks},
    };

    for (const SideSource& source : sources) {
        simdjson::ondemand::array rows;
        if (doc[source.key].get_array().get(rows)) {
            return Result<BookSnapshot, SnapshotError>::failure(source.missing_error);
        }
        for (auto row_result : rows) {
            std::string_view fields[3];
            const auto read = readRowFields(row_result.value(), fields);
            if (!read.hasValue()) {
                return read;
            }

            const auto parsed = parseRow(fields, spec, source.side);
            if (!parsed.hasValue()) {
                return Result<BookSnapshot, SnapshotError>::failure(*parsed.errorIf());
            }
            const SnapshotOrder& order = *parsed.valueIf();

            if (!seen_ids.insert(order.order_id).second) {
                return Result<BookSnapshot, SnapshotError>::failure(
                    SnapshotError::duplicate_order_id);
            }

            book.orders.push_back(order);
        }
    }

    return Result<BookSnapshot, SnapshotError>::success(book);
}

}  // namespace te::bitstamp
