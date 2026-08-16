#include <te/feed/bitstamp_snapshot.hpp>
#include <string_view>
#include "simdjson/padded_string.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string_view.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/ondemand.h"
#include "te/core/text_to_int.hpp"

namespace te{

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

    Result<BookSnapshot, SnapshotError> parseBitstampSnapshot(std::string_view text,
                                                            InstrumentSpec spec)
    {
        SnapshotOrder snapshotOrder;
        BookSnapshot book;
        simdjson::ondemand::parser parser;
        simdjson::padded_string buffer = simdjson::padded_string(text);

        simdjson::ondemand::document doc;
        simdjson::error_code err = parser.iterate(buffer).get(doc);

        if (err) {
            // couldn't even parse this as JSON at all
            return te::Result<BookSnapshot, SnapshotError>::failure(te::SnapshotError::malformed_json);
        }

        std::string_view microtimestamp;
        if (doc["microtimestamp"].get_string().get(microtimestamp)) {
            return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::missing_microtimestamp);
        }
        book.microtimestamp = *(parseInteger(microtimestamp).valueIf());

        simdjson::ondemand::array bids;
        if (doc["bids"].get_array().get(bids)) {
            return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::missing_bids);
        }
        for (auto row_result : bids) {
            std::string_view fields[3];
            auto read = readRowFields(row_result.value(), fields);
            if (!read.hasValue()) {
                return read;
            }

            //Price
            const int64_t *snapshot_price = te::parseDecimal(fields[0], spec.price_decimals).valueIf();
            if (snapshot_price == nullptr){
                    return Result<BookSnapshot, SnapshotError>::failure(te::SnapshotError::invalid_price);
                } else {
                    snapshotOrder.price = Price{*snapshot_price};
                }

            //Qty
            const int64_t *snapshot_qty = te::parseDecimal(fields[1], spec.quantity_decimals).valueIf();
            if (snapshot_qty == nullptr){
                    return Result<BookSnapshot, SnapshotError>::failure(te::SnapshotError::invalid_quantity);
                } else {
                    snapshotOrder.quantity = Qty{*snapshot_qty};
                }
            //ID
            const uint64_t *snapshot_ID = te::parseInteger(fields[2]).valueIf();
            if (snapshot_ID == nullptr){
                    return Result<BookSnapshot, SnapshotError>::failure(te::SnapshotError::invalid_order_id);
                } else {
                    snapshotOrder.order_id = OrderId{*snapshot_ID};
                }

            //Side
            snapshotOrder.side = te::Side::buy;

            book.orders.push_back(snapshotOrder);

        }

        simdjson::ondemand::array asks;
        if (doc["asks"].get_array().get(asks)) {
            return Result<BookSnapshot, SnapshotError>::failure(SnapshotError::missing_asks);
        }
        for (auto row_result : asks) {
            std::string_view fields[3];
            auto read = readRowFields(row_result.value(), fields);
            if (!read.hasValue()) {
                return read;
            }

            //Price
            const int64_t *snapshot_price = te::parseDecimal(fields[0], spec.price_decimals).valueIf();
            if (snapshot_price == nullptr){
                    return Result<BookSnapshot, SnapshotError>::failure(te::SnapshotError::invalid_price);
                } else {
                    snapshotOrder.price = Price{*snapshot_price};
                }

            //Qty
            const int64_t *snapshot_qty = te::parseDecimal(fields[1], spec.quantity_decimals).valueIf();
            if (snapshot_qty == nullptr){
                    return Result<BookSnapshot, SnapshotError>::failure(te::SnapshotError::invalid_quantity);
                } else {
                    snapshotOrder.quantity = Qty{*snapshot_qty};
                }
            //ID
            const uint64_t *snapshot_ID = te::parseInteger(fields[2]).valueIf();
            if (snapshot_ID == nullptr){
                    return Result<BookSnapshot, SnapshotError>::failure(te::SnapshotError::invalid_order_id);
                } else {
                    snapshotOrder.order_id = OrderId{*snapshot_ID};
                }
              //Side
            snapshotOrder.side = te::Side::sell;

            book.orders.push_back(snapshotOrder);
        }

        return Result<BookSnapshot, SnapshotError>::success(book);

    }

}
