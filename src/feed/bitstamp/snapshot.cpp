#include <te/feed/bitstamp/snapshot.hpp>
#include <string_view>
#include <unordered_set>
#include "simdjson/padded_string.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string_view.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/ondemand.h"
#include "te/core/text_to_int.hpp"

namespace te::bitstamp {

    // Reads a snapshot row's 3 string fields in one forward pass over its own elements. On
    // Demand is single-pass/forward-only; row.at(index) does not compose safely with continuing
    // to iterate afterward (verified: crashes with an internal assertion once used across
    // multiple rows followed by a second top-level array), so every field is read via one
    // ordinary range-for instead.
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

    // Parses one [price, quantity, order_id] row into a SnapshotOrder on the given side.
    //
    // Every parse result is held in a NAMED local before valueIf() is called on it. Writing
    // `parseDecimal(...).valueIf()` instead would take the address of a temporary that is
    // destroyed at that same semicolon, leaving a dangling pointer -- undefined behaviour that
    // happens to return correct values most of the time, which is exactly what makes it
    // dangerous. GCC at -O2 flags it; a debug build and a passing end-to-end run do not.
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

    Result<BookSnapshot, SnapshotError> parseSnapshot(std::string_view text, InstrumentSpec spec)
    {
        BookSnapshot book;
        simdjson::ondemand::parser parser;
        simdjson::padded_string buffer = simdjson::padded_string(text);

        simdjson::ondemand::document doc;
        simdjson::error_code err = parser.iterate(buffer).get(doc);

        if (err) {
            // couldn't even parse this as JSON at all
            return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::malformed_json);
        }

        std::string_view microtimestamp;
        if (doc["microtimestamp"].get_string().get(microtimestamp)) {
            return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::missing_microtimestamp);
        }
        // Named local, then a null check: parseInteger can fail, and dereferencing its result
        // unchecked would read through nullptr on any malformed timestamp.
        const auto microtimestamp_result = parseInteger(microtimestamp);
        const std::uint64_t* microtimestamp_value = microtimestamp_result.valueIf();
        if (microtimestamp_value == nullptr) {
            return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::missing_microtimestamp);
        }
        book.microtimestamp = *microtimestamp_value;

        // A duplicate id means the snapshot cannot be trusted as a starting state, and the
        // parser must not leave the caller to guess which copy is real (see the header).
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
