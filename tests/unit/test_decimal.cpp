#include <gtest/gtest.h>
#include <iostream>

#include <te/core/text_to_int.hpp>


// ---- happy-path tests:
TEST(Decimal, ParsesBitstampPriceAtScale2) {
    std::string_view text{"58356.10"};
    std::uint8_t scale = 2;
    const auto result = te::parseDecimal(text, scale); 
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 5835610);
}

TEST(Decimal, ParsesBitstampAmountAtScale8) {
    std::string_view text{"58356.10001000"};
    std::uint8_t scale = 8;
    const auto result = te::parseDecimal(text, scale); 
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 5835610001000);
}
TEST(Decimal, PadsShortFractionToScale) {
    std::string_view text{"58356.1"};
    std::uint8_t scale = 2;
    const auto result = te::parseDecimal(text, scale); 
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 5835610);
}

TEST(Decimal, AcceptsIntegerWithNoDot) {
    std::string_view text{"58356"};
    std::uint8_t scale = 2;
    const auto result = te::parseDecimal(text, scale); 
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 5835600);
}

TEST(Decimal, ScaleZeroReturnsWholeNumber) {
    std::string_view text{"58356"};
    std::uint8_t scale = 0;
    const auto result = te::parseDecimal(text, scale); 
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 58356);
}


TEST(Decimal, ParsesZero) {
    std::string_view text{"0.00"};
    std::uint8_t scale = 2;
    const auto result = te::parseDecimal(text, scale); 
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 0);
}


// ---— error-path tests

TEST(Decimal, RejectsExcessPrecisionAtScale4) {
    std::string_view text{"58356.10111"};
    std::uint8_t scale = 4;
    const auto result = te::parseDecimal(text, scale);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::excess_precision);
}

TEST(Decimal, RejectsNegativeNotAllowed) {
    std::string_view text{"-58356.10111"};
    std::uint8_t scale = 4;
    const auto result = te::parseDecimal(text, scale);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::negative_not_allowed);
}

TEST(Decimal, RejectsEmptyInput) {
    std::string_view text{""};
    std::uint8_t scale = 4;
    const auto result = te::parseDecimal(text, scale);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::empty_input);
}

TEST(Decimal, RejectsNonDigitCharacter) {
    std::string_view text{"4242a.112,"};
    std::uint8_t scale = 4;
    const auto result = te::parseDecimal(text, scale);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::invalid_character);
}

TEST(Decimal, RejectsSecondDecimalPoint) {
    std::string_view text{"42.42.112"};
    std::uint8_t scale = 4;
    const auto result = te::parseDecimal(text, scale);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::invalid_format);
}

TEST(Decimal, RejectsUnsupportedScale) {
    std::string_view text{"4242.112"};
    std::uint8_t scale = 19;
    const auto result = te::parseDecimal(text, scale);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::unsupported_scale);
}

TEST(Decimal, RejectsLoneDot) {
    std::string_view text{"."};
    std::uint8_t scale = 2;
    const auto result = te::parseDecimal(text, scale);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::invalid_format);
}

//---— boundary tests:

TEST(Decimal,AcceptsMaximumSupportedScale){
    std::string_view text{"1.0"};
    std::uint8_t scale = 18;
    const auto result = te::parseDecimal(text, scale); 
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 1000000000000000000);
}

TEST(Decimal,AcceptsExactlyInt64Max){
    std::string_view text{"92233720368547758.07"};
    std::uint8_t scale = 2;
    const auto result = te::parseDecimal(text, scale); 
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 9223372036854775807);
}

TEST(Decimal,RejectsOneAboveInt64Max){
    std::string_view text{"92233720368547758.08"};
    std::uint8_t scale = 2;
    const auto result = te::parseDecimal(text, scale); 
    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::overflow);
}

TEST(Decimal,RejectsOverflowingWholePart){
    std::string_view text{"92233720368547759.00"};
    std::uint8_t scale = 2;
    const auto result = te::parseDecimal(text, scale);
    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::overflow);
}

// ---- parseInteger: happy-path tests

TEST(ParseInteger, ParsesBitstampOrderId) {
    std::string_view id_str{"2037493297635328"};
    const auto result = te::parseInteger(id_str);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 2037493297635328ULL);
}

TEST(ParseInteger, ParsesBitstampMicrotimestamp) {
    std::string_view microtimestamp{"1786269861947000"};
    const auto result = te::parseInteger(microtimestamp);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 1786269861947000ULL);
}

TEST(ParseInteger, ParsesZero) {
    std::string_view id_str{"0"};
    const auto result = te::parseInteger(id_str);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 0);
}

// ---- parseInteger: error-path tests

TEST(ParseInteger, RejectsEmptyInput) {
    std::string_view id_str{""};
    const auto result = te::parseInteger(id_str);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::empty_input);
}

TEST(ParseInteger, RejectsNegative) {
    std::string_view id_str{"-2037493297635328"};
    const auto result = te::parseInteger(id_str);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::negative_not_allowed);
}

TEST(ParseInteger, RejectsNonDigitCharacter) {
    std::string_view id_str{"203a749"};
    const auto result = te::parseInteger(id_str);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::invalid_character);
}

// ---- parseInteger: boundary tests

TEST(ParseInteger, AcceptsExactlyUint64Max) {
    std::string_view id_str{"18446744073709551615"};
    const auto result = te::parseInteger(id_str);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), 18446744073709551615ULL);
}

TEST(ParseInteger, RejectsOneAboveUint64Max) {
    std::string_view id_str{"18446744073709551616"};
    const auto result = te::parseInteger(id_str);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ParseError::overflow);
}

