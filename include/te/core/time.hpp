#pragma once

#include <chrono>
#include <functional>

namespace te {

/**
 * @brief  Nanoseconds; the representation for every timestamp in this codebase.
 */
using Nanos = std::chrono::nanoseconds;

/**
 * @brief  The single seam through which this codebase reads the current time.
 *
 * @note   Nothing else may call std::chrono directly. Centralising it here is what lets a
 *         test substitute a controlled clock and assert an exact timestamp, rather than
 *         asserting that some real, unpredictable value looks plausible.
 *
 * @note   Holds callables rather than deriving from an abstract base: there is no virtual
 *         dispatch anywhere in this codebase, and a struct of std::function matches the
 *         value-type style used by Result, Price and Qty.
 */
struct Clock {
    /**
     * @brief  Wall-clock time since the Unix epoch.
     *
     * @note   Aligns with venue-reported timestamps (see OrderEvent::venue_timestamp_us) and
     *         is what a Record's own receipt timestamp uses. May jump backward if the system
     *         clock is corrected, so it must not be used to measure durations.
     *
     * @throws std::bad_function_call If invoked on a Clock whose `now` was never assigned.
     */
    std::function<Nanos()> now;

    /**
     * @brief  Monotonic time since an unspecified reference point.
     *
     * @note   Never runs backward, which is what makes it correct for measuring durations and
     *         latency. Its absolute value is meaningless -- only differences between two
     *         readings carry information -- because its epoch is implementation-defined.
     *
     * @note   Reserved for the Slice 4 latency work; nothing calls it yet.
     *
     * @throws std::bad_function_call If invoked on a Clock whose `steadyNow` was never
     *         assigned.
     */
    std::function<Nanos()> steadyNow;
};

/**
 * @brief  Builds the one real, system-backed Clock.
 *
 * @return A Clock whose `now` reads std::chrono::system_clock and whose `steadyNow` reads
 *         std::chrono::steady_clock, both converted to nanoseconds.
 *
 * @note   The only place in this codebase where std::chrono::system_clock and
 *         std::chrono::steady_clock may be named.
 */
Clock makeSystemClock();

}  // namespace te
