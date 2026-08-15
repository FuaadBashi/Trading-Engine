#pragma once

#include <cstdint>
#include <type_traits>

#include <te/core/time.hpp>
#include <te/feed/events.hpp>

// Slice 1. Fixed-size binary record layout. Versioned. static_assert the size.

namespace te {

/**
 * @brief  On-disk format version stamped into every Record this build writes.
 *
 * @note   Bump this whenever Record's layout changes, and keep a decoder for the old layout;
 *         capture files written before the change still carry the old one.
 */
constexpr std::uint8_t kCurrentRecordVersion = 1;

/**
 * @brief  One captured market event, in the fixed-size binary layout written to a capture file.
 *
 * @note   Deliberately its own type rather than writing OrderEvent straight to disk.
 *         OrderEvent's layout is free to change for in-memory reasons -- new fields, field
 *         reordering for cache behaviour -- and keeping the two separate means such a change
 *         cannot silently alter what a capture file means.
 *
 * @note   Fixed size is what makes "seek to record N" pure arithmetic rather than a scan, the
 *         same property Kafka's segment files rely on. It is also why the size is asserted
 *         below rather than left to whatever the compiler happens to lay out.
 *
 * @note   The current version's read/write path is a plain memcpy, valid only because this
 *         type is trivially copyable. Reading an older version, once one exists, goes through
 *         explicit offset-based decoding (byte_buffer.hpp) instead of assuming this layout.
 */
struct Record {
    /** @brief The decoded event: venue timestamp, order id, price, quantity, side, kind. */
    OrderEvent orderEvent;

    /**
     * @brief  Format version this record was written with.
     *
     * @note   Must be read first. A reader cannot interpret the remaining bytes without
     *         knowing which layout produced them.
     */
    std::uint8_t version;

    /**
     * @brief  When this machine received the event, per Clock::now().
     *
     * @note   Distinct from OrderEvent::venue_timestamp_us, which is the venue's own clock.
     *         The difference between the two is network plus processing delay, which is what
     *         the Slice 4 latency work will measure.
     */
    Nanos receipt_timestamp_us;
};

static_assert(sizeof(Record) == 56,
              "Record must stay 56 bytes; if a field changed, bump version and update the on-disk format.");
static_assert(std::is_trivially_copyable_v<Record>,
              "Record must remain trivially copyable for the memcpy fast path.");

/**
 * @brief  Builds a Record from a decoded event, stamping the version and receipt time.
 *
 * @param  orderEvent The decoded event, copied in unchanged.
 * @param  clock      Supplies the receipt timestamp via its now() callable.
 *
 * @return A Record carrying @p orderEvent, kCurrentRecordVersion, and the time read from
 *         @p clock.
 *
 * @throws std::bad_function_call If @p clock has no `now` assigned. A Clock from
 *         makeSystemClock() always does; a hand-built one used in a test may not.
 */
Record buildRecord(const OrderEvent& orderEvent, const Clock& clock);

}  // namespace te
