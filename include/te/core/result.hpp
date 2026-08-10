#pragma once

#include <utility>
#include <variant>

namespace te {

/// Represents an operation that produces either a value of type T or a reasoned error of type E.
///
/// This small type is used at fallible boundaries such as decimal parsing. It deliberately exposes
/// pointer-returning queries instead of throwing when the caller asks for the inactive alternative.
/// T and E must be different types because the implementation queries std::variant by type.
template <typename T, typename E>
class Result {
public:
    /// Creates a successful result containing value.
    static Result success(T value) { return Result(std::move(value)); }

    /// Creates a failed result containing error.
    static Result failure(E error) { return Result(std::move(error)); }

    /// Returns true when this object currently contains a successful T value.
    bool has_value() const { return std::holds_alternative<T>(storage_); }

    /// Returns a pointer to the successful value, or nullptr when this object contains an error.
    const T* value_if() const { return std::get_if<T>(&storage_); }

    /// Returns a pointer to the error, or nullptr when this object contains a successful value.
    const E* error_if() const { return std::get_if<E>(&storage_); }

private:
    // Exactly one alternative is live at a time; no heap allocation is needed by Result itself.
    std::variant<T, E> storage_;

    // Construction is private so callers must state success(...) or failure(...).
    explicit Result(T value) : storage_{std::move(value)} {}
    explicit Result(E error) : storage_{std::move(error)} {}
};
}  // namespace te
