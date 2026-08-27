#pragma once

#include <utility>
#include <variant>

namespace te {

// Explicit success-or-error value used where callers need a failure reason (ADR 0002/0003).
// T and E must differ because the variant is queried by type.
template <typename T, typename E>
class Result {
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(E error) { return Result(std::move(error)); }

    bool hasValue() const { return std::holds_alternative<T>(storage_); }

    // Pointer access makes querying the inactive alternative non-throwing; check before use.
    const T* valueIf() const { return std::get_if<T>(&storage_); }
    T* valueIf() { return std::get_if<T>(&storage_); }
    const E* errorIf() const { return std::get_if<E>(&storage_); }

private:
    std::variant<T, E> storage_;

    // Private constructors force call sites to state success or failure explicitly.
    explicit Result(T value) : storage_{std::move(value)} {}
    explicit Result(E error) : storage_{std::move(error)} {}
};

}  // namespace te
