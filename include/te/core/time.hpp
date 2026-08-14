#pragma once

#include <chrono>
#include <functional>


namespace te {

/// Nanoseconds. The representation for every timestamp in this codebase.
using Nanos = std::chrono::nanoseconds;

/// Central seam for reading time. Nothing else in this codebase may call std::chrono directly --
/// this is the one place "what time is it" is decided, so a test can substitute a controlled
/// clock instead of the real one.
///
/// now() is wall-clock time: it aligns with venue-reported timestamps (see OrderEvent::
/// venue_timestamp_us) and is what a Record's own timestamp should use. steadyNow() is
/// monotonic -- it never runs backward, which matters for measuring durations/latency, but is
/// not tied to a real calendar time. for now only needs now(); steadyNow()
/// exists for the Slice 4 latency work and is not used yet.
struct Clock {
    std::function<Nanos()> now;
    std::function<Nanos()> steadyNow;
};

/// Returns the one real, system-backed Clock. The only place std::chrono::system_clock and
/// std::chrono::steady_clock may be named in this codebase.
Clock makeSystemClock();

}  // namespace te
