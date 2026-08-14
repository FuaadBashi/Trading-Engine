#pragma once

#include <cstdint>
#include <string_view>
#include <te/core/result.hpp>

namespace te {

/// Reason a decimal text value could not be converted into exact integer units.
///
/// This vocabulary will be returned by the planned exact decimal parser. Keeping the reason lets a
/// decoder distinguish malformed venue data from an unsupported market rule or numeric overflow.
enum class ParseError {
    /// No input characters were supplied.
    empty_input,

    /// A character outside the accepted decimal grammar was encountered.
    invalid_character,

    /// The characters are individually valid but have invalid structure.
    invalid_format,

    /// The supplied boundary accepts only non-negative decimal text.
    negative_not_allowed,

    /// The fractional component has more digits than the requested scale permits.
    excess_precision,

    /// The requested scale lies outside the parser's explicitly supported range.
    unsupported_scale,

    /// The exact scaled integer cannot be represented by std::int64_t.
    overflow,
};

/// Converts decimal text into exact integer units at scale fractional digits.
///
/// scale is the number of fractional digits the caller wants preserved; the returned int64 is the
/// value of text shifted by that many decimal places, e.g. "1.20" at scale 8 yields 120'000'000.
te::Result<std::int64_t, ParseError> parseDecimal(std::string_view text, std::uint8_t scale);
 
te::Result<std::uint64_t, ParseError> parseInteger(std::string_view id_str);

}  // namespace te
