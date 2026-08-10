#include <gtest/gtest.h>

#include <te/core/instrument.hpp>

// Verified Bitstamp BTC/USD metadata. Decimal scales are intentionally data, not global constants.
TEST(InstrumentSpec, StoresBitstampBtcUsdScales) {
    const te::InstrumentSpec btc_usd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };

    EXPECT_EQ(btc_usd.venue_id, te::VenueId::bitstamp);
    EXPECT_EQ(btc_usd.instrument_id, te::InstrumentId::btc_usd);
    EXPECT_EQ(btc_usd.price_decimals, 2);
    EXPECT_EQ(btc_usd.quantity_decimals, 8);
}

TEST(InstrumentSpec, ScalesArePerSpecification) {
    const te::InstrumentSpec btc_usd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };

    // Deliberately synthetic scale values: this fixture does not claim that a real
    // venue uses them. It proves the representation is not a hidden global 2/8 rule.
    const te::InstrumentSpec synthetic{
        .venue_id = te::VenueId::unknown,
        .instrument_id = te::InstrumentId::unknown,
        .price_decimals = 4,
        .quantity_decimals = 3,
    };

    EXPECT_NE(synthetic.price_decimals, btc_usd.price_decimals);
    EXPECT_NE(synthetic.quantity_decimals, btc_usd.quantity_decimals);
    EXPECT_EQ(synthetic.price_decimals, 4);
    EXPECT_EQ(synthetic.quantity_decimals, 3);
}
