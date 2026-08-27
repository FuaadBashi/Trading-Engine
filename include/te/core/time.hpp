#pragma once

#include <chrono>
#include <functional>

namespace te {

using Nanos = std::chrono::nanoseconds;

// Injectable clock seam: production uses makeSystemClock; tests supply deterministic callables.
struct Clock {
    // Epoch time for record provenance; may jump and must not measure durations.
    std::function<Nanos()> now;

    // Monotonic time for durations; only differences are meaningful.
    std::function<Nanos()> steadyNow;
};

// The only production function that reads std::chrono clocks directly.
Clock makeSystemClock();

}  // namespace te
