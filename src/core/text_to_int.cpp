#include "te/core/text_to_int.hpp"

namespace te {

te::Result<std::int64_t, te::ParseError> parseDecimal(std::string_view text, std::uint8_t scale) {
    int64_t parsed_text_before_decimal = 0;
    int64_t parsed_text_after_decimal = 0;
    int fractionalDigitCount = 0;
    bool decimal = false;

    if (text.size() == 0) {
        return te::Result<std::int64_t, te::ParseError>::failure(te::ParseError::empty_input);
    }
    if (scale > 18) {
        return te::Result<std::int64_t, te::ParseError>::failure(te::ParseError::unsupported_scale);
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (decimal && text[i] == '.') {
            return te::Result<std::int64_t, te::ParseError>::failure(
                te::ParseError::invalid_format);
        }
        if (text.size() == 1 && text[i] == '.') {
            return te::Result<std::int64_t, te::ParseError>::failure(
                te::ParseError::invalid_format);
        }

        if (text[i] == '-') {
            return te::Result<std::int64_t, te::ParseError>::failure(
                te::ParseError::negative_not_allowed);
        }
        if (text[i] != '.' && (text[i] < '0' || text[i] > '9')) {
            return te::Result<std::int64_t, te::ParseError>::failure(
                te::ParseError::invalid_character);
        }
        if (!decimal) {
            if (text[i] == '.') {
                decimal = true;
                continue;
            }
            int x = static_cast<int>(text[i] - '0');
            parsed_text_before_decimal *= (10);
            parsed_text_before_decimal += x;
        } else {
            int x = static_cast<int>(text[i] - '0');
            parsed_text_after_decimal *= (10);
            parsed_text_after_decimal += x;
            ++fractionalDigitCount;
        }
    }
    if (fractionalDigitCount > scale) {
        return te::Result<std::int64_t, te::ParseError>::failure(te::ParseError::excess_precision);
    }
    if (fractionalDigitCount < scale) {
        int number_of_zeros = scale - fractionalDigitCount;
        for (int j = 0; j < number_of_zeros; ++j) {
            parsed_text_after_decimal *= 10;
        }
    }
    int64_t multiplier = 1;
    for (int k = 0; k < scale; ++k) {
        multiplier *= 10;
    }

    int64_t maxWholeAllowed = INT64_MAX / multiplier;
    if (parsed_text_before_decimal > maxWholeAllowed) {
        return te::Result<std::int64_t, te::ParseError>::failure(te::ParseError::overflow);
    }
    int64_t scaledWhole = parsed_text_before_decimal * multiplier;  

    if (scaledWhole > INT64_MAX - parsed_text_after_decimal) {
        return te::Result<std::int64_t, te::ParseError>::failure(te::ParseError::overflow);
    }

    return te::Result<std::int64_t, te::ParseError>::success(scaledWhole +
                                                             parsed_text_after_decimal);
};


te::Result<std::uint64_t, ParseError> parseInteger(std::string_view id_str){

    uint64_t parsed_int {0};
    uint64_t maxWholeAllowed = UINT64_MAX / 10;

    if (id_str.size() == 0) {
        return te::Result<std::uint64_t, te::ParseError>::failure(te::ParseError::empty_input);
    }
    for (std::size_t i = 0; i < id_str.size(); ++i) {
         if (id_str[i] == '-') {
            return Result<std::uint64_t, te::ParseError>::failure(te::ParseError::negative_not_allowed);
        }
        if (parsed_int > maxWholeAllowed){
            return Result<std::uint64_t, te::ParseError>::failure(te::ParseError::overflow);
        }
        if (id_str[i] < '0' || id_str[i] > '9'){
            return Result<std::uint64_t, te::ParseError>::failure(te::ParseError::invalid_character);
        }
        parsed_int *= 10;
        uint64_t digit = static_cast<uint64_t>(id_str[i] - '0');
        if (parsed_int > UINT64_MAX - digit) {
            return Result<std::uint64_t, te::ParseError>::failure(ParseError::overflow);
        }
        parsed_int += digit;
    }

    return Result<std::uint64_t, te::ParseError>::success(parsed_int);
}
}  // namespace te
