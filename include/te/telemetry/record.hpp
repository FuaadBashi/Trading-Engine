#pragma once
#include <cstdint>
#include <chrono>
#include "te/feed/events.hpp"
#include <te/core/time.hpp>

// Slice 1. Fixed-size binary record layout. Versioned. static_assert the size.
// TODO(fuaad): write this yourself. Declarations first, then the test, then the body.

namespace te {

/// One captured market event, in the fixed-size binary layout written to a capture file.
///
/// This is the on-disk/wire representation, deliberately kept as its own type rather than
/// writing OrderEvent straight to disk: OrderEvent's layout is free to change for in-memory
/// reasons (cache behaviour, new fields) without silently changing what a capture file means.
/// The current version's read/write path is a plain memcpy (see byte_buffer.hpp for why that's
/// safe here); reading an older version, once one exists, goes through explicit offset-based
/// decoding instead of assuming the current struct's layout.
struct Record {
    /// The decoded event itself: venue timestamp, order id, price, quantity, side, kind.
    OrderEvent orderEvent;

    /// Format version this record was written with. Always read first; a reader must not
    /// interpret the remaining bytes without knowing which layout they were written under.
    std::uint8_t version;

    /// When this machine received/processed the event, per Clock::now() -- distinct from
    /// OrderEvent::venue_timestamp_us, which is the venue's own clock, not this one.
    Nanos receipt_timestamp_us;

};
constexpr std::uint8_t kCurrentRecordVersion = 1;

static_assert(sizeof(Record) == 56,
              "Record must stay 56 bytes; if a field changed, bump version and update the on-disk format.");
static_assert(std::is_trivially_copyable_v<Record>,
              "Record must remain trivially copyable for the memcpy fast path.");

/// Builds a Record from a decoded event, stamping receipt_timestamp_us via clock.now() and
/// version with the current on-disk format version.
Record buildRecord(const OrderEvent& orderEvent, const Clock& clock);

}  // namespace te
