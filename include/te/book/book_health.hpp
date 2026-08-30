#pragma once
#include <cstddef>



namespace te {

enum class BookHealthState {
    unseeded,
    synchronizing,
    valid,
    corrupted,
    fatal_failure
};

enum class FailureReason {
    none,
    event_gap,
    disconnect,
    snapshot_failure,
    replay_failure
};

class BookHealth {
public:

bool startSynchronization();
bool synchronizationSucceeded();
bool synchronizationFailed(FailureReason reason);
bool markCorrupted(FailureReason reason);

bool isUsable() const;

BookHealthState getState() const {return state_;}

FailureReason getFailureReason() const {return failureReason_;}

std::size_t getFailureCount() const {return consecutiveFailureCount_;}


    

private:
    BookHealthState state_{BookHealthState::unseeded};
    FailureReason failureReason_{FailureReason::none};
    std::size_t consecutiveFailureCount_{};
};




}  // namespace te