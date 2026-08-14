#include <te/core/time.hpp>

namespace te {

Clock makeSystemClock(){
    Clock clock;

    clock.now = []() {
    return std::chrono::duration_cast<Nanos>(std::chrono::system_clock::now().time_since_epoch());
    };
    
    clock.steadyNow = []() {
    return std::chrono::duration_cast<Nanos>(std::chrono::steady_clock::now().time_since_epoch());
    };

    return clock;
};

}