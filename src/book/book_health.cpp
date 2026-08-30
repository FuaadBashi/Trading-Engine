
#include "te/book/book_health.hpp"
namespace te {


bool BookHealth::startSynchronization(){

    if (state_ == BookHealthState::unseeded) {
        state_ = BookHealthState::synchronizing;
        failureReason_ = FailureReason::none;
        return true;
    }

    if (state_ == BookHealthState::corrupted) {
        state_ = BookHealthState::synchronizing;
        return true;
    }

    return false;
    
}

bool BookHealth::synchronizationSucceeded() {
    if (state_ != BookHealthState::synchronizing) {
        return false;
    }

    state_ = BookHealthState::valid;
    failureReason_ = FailureReason::none;
    consecutiveFailureCount_ = 0;
    return true;
}


bool BookHealth::synchronizationFailed(FailureReason reason) {
    if (state_ != BookHealthState::synchronizing ||
        reason == FailureReason::none) {
        return false;
    }

    failureReason_ = reason;
    ++consecutiveFailureCount_;

    if (consecutiveFailureCount_ >= 3) {
        state_ = BookHealthState::fatal_failure;
    } else {
        state_ = BookHealthState::corrupted;
    }

    return true;
}

bool BookHealth::markCorrupted(FailureReason reason) {
    if (state_ != BookHealthState::valid ||
        reason == FailureReason::none) {
        return false;
    }

    state_ = BookHealthState::corrupted;
    failureReason_ = reason;
    return true;
}

bool BookHealth::isUsable() const {
    return state_ == BookHealthState::valid;
}


}