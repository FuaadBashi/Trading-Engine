#pragma once

#include <utility>
#include <variant>

namespace te {

/**
 * @brief  Holds either a value of type @p T or a reasoned error of type @p E, never both.
 *
 * @tparam T Successful value type.
 * @tparam E Error type, typically a scoped enum naming the failure reasons.
 *
 * @note   Used at fallible boundaries such as decimal parsing and capture-file opening. It
 *         deliberately exposes pointer-returning queries rather than throwing when the caller
 *         asks for the inactive alternative, per ADR 0002.
 *
 * @note   @p T and @p E must be different types, because the implementation queries
 *         `std::variant` by type.
 */
template <typename T, typename E>
class Result {
public:
    /**
     * @brief  Creates a successful result containing @p value.
     *
     * @param  value The successful value, moved into the result.
     *
     * @return A Result holding @p value.
     */
    static Result success(T value) { return Result(std::move(value)); }

    /**
     * @brief  Creates a failed result containing @p error.
     *
     * @param  error The reason the operation failed, moved into the result.
     *
     * @return A Result holding @p error.
     */
    static Result failure(E error) { return Result(std::move(error)); }

    /**
     * @brief  Reports whether this object currently holds a successful value.
     *
     * @return True when a @p T is held, false when an @p E is held.
     */
    bool hasValue() const { return std::holds_alternative<T>(storage_); }

    /**
     * @brief  Accesses the successful value.
     *
     * @return Pointer to the value, or nullptr when this object holds an error.
     *
     * @note   Callers must check for nullptr before dereferencing; that check is the whole
     *         reason this returns a pointer rather than a reference.
     */
    const T* valueIf() const { return std::get_if<T>(&storage_); }

    /**
     * @brief  Accesses the successful value for modification.
     *
     * @return Pointer to the value, or nullptr when this object holds an error.
     *
     * @note   Exists so a Result may hold a resource the caller still needs to use --
     *         `Result<Sink, SinkError>`, for example, where a Sink exists to be written to.
     *         Pure-value callers keep resolving to the const overload above.
     */
    T* valueIf() { return std::get_if<T>(&storage_); }

    /**
     * @brief  Accesses the error.
     *
     * @return Pointer to the error, or nullptr when this object holds a successful value.
     */
    const E* errorIf() const { return std::get_if<E>(&storage_); }

private:
    // Exactly one alternative is live at a time; Result itself needs no heap allocation.
    std::variant<T, E> storage_;

    // Construction is private so callers must state success(...) or failure(...).
    explicit Result(T value) : storage_{std::move(value)} {}
    explicit Result(E error) : storage_{std::move(error)} {}
};

}  // namespace te
