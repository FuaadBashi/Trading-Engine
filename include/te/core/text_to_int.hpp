#pragma once

#include <cstdint>
#include <string_view>
#include <te/core/result.hpp>

namespace te {

// Exact parsing failures remain distinct so decoders can report malformed data vs overflow.
enum class ParseError {
    empty_input,
    invalid_character,
    invalid_format,
    negative_not_allowed,
    excess_precision,
    unsupported_scale,
    overflow,
};

// Parses non-negative decimal text directly into scaled integer units: no floating-point
// round trip. Short fractions are zero-padded; excess precision is rejected, never rounded.
te::Result<std::int64_t, ParseError> parseDecimal(std::string_view text, std::uint8_t scale);

// Parses digits-only IDs/timestamps with pre-operation overflow checks.
te::Result<std::uint64_t, ParseError> parseInteger(std::string_view id_str);

}  // namespace te
