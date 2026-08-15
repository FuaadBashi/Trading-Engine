#pragma once

#include <cstdint>
#include <string_view>

#include <te/core/result.hpp>

namespace te {

/**
 * @brief  Reason venue text could not be converted into an exact integer.
 *
 * @note   Keeping the reason lets a decoder distinguish malformed venue data from an
 *         unsupported market rule or a numeric overflow, rather than collapsing all three
 *         into one opaque failure.
 */
enum class ParseError {
    /** @brief No input characters were supplied. */
    empty_input,

    /** @brief A character outside the accepted grammar was encountered. */
    invalid_character,

    /** @brief Characters are individually valid but arranged wrongly, e.g. "1.2.3" or ".". */
    invalid_format,

    /** @brief A leading '-' was found; these parsers accept non-negative text only. */
    negative_not_allowed,

    /** @brief The fractional component has more digits than the requested scale permits. */
    excess_precision,

    /** @brief The requested scale lies outside the parser's explicitly supported range. */
    unsupported_scale,

    /** @brief The exact scaled integer cannot be represented in the return type. */
    overflow,
};

/**
 * @brief  Converts decimal text into exact integer units at a given scale.
 *
 * @param  text  Decimal text as the venue sent it, e.g. "58356.10". Non-negative, digits and
 *               at most one '.' only.
 * @param  scale Fractional digits to preserve. Supported range is 0 to 18 inclusive; above
 *               that, 10^scale alone would exceed std::int64_t.
 *
 * @return The value of @p text shifted left by @p scale decimal places, or a ParseError.
 *         "58356.10" at scale 2 yields 5'835'610; "1.20" at scale 8 yields 120'000'000.
 *
 * @note   Uses no floating point anywhere. Routing venue prices through a double would put
 *         binary rounding error directly on the price path, which is the failure ADR 0004
 *         exists to prevent.
 *
 * @note   Fewer fractional digits than @p scale are zero-padded; more are rejected as
 *         ParseError::excess_precision rather than rounded, because silently discarding
 *         precision a venue sent is never safe to guess at.
 *
 * @note   Signed return, matching Price::ticks and Qty::units, which are signed so that the
 *         difference of two of them is representable.
 */
te::Result<std::int64_t, ParseError> parseDecimal(std::string_view text, std::uint8_t scale);

/**
 * @brief  Converts a run of decimal digits into an exact integer, with overflow checking.
 *
 * @param  id_str Digits only, no sign and no decimal point. Suits venue order ids and
 *                microsecond timestamps, which arrive as strings.
 *
 * @return The parsed value, or a ParseError.
 *
 * @note   Unsigned return, matching OrderId::value: the values this parses are opaque labels
 *         that are only ever compared for equality, never subtracted, so the full 64-bit
 *         positive range is usable without wraparound risk.
 *
 * @note   Overflow is checked before each multiply and add rather than detected afterward.
 *         Signed overflow is undefined behaviour, and unsigned wraparound produces a
 *         plausible-looking value that a later check cannot distinguish from a real one.
 */
te::Result<std::uint64_t, ParseError> parseInteger(std::string_view id_str);

}  // namespace te
