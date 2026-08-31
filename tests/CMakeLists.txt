# Unit-test target convention: compile one test source, link project code plus GoogleTest, then
# let CMake discover every TEST(Suite, Name) case for CTest.

include(GoogleTest)

function(te_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE te_core GTest::gtest_main)
    te_set_warnings(${name})
    te_enable_sanitizers(${name})
    gtest_discover_tests(${name}
    DISCOVERY_TIMEOUT 30
)
endfunction()

te_add_test(test_types unit/test_types.cpp)   # start here: one trivial assertion, green in CI
te_add_test(test_events unit/test_events.cpp)
te_add_test(test_instrument unit/test_instrument.cpp)
te_add_test(test_result unit/test_result.cpp)
te_add_test(test_decimal unit/test_decimal.cpp)
te_add_test(test_decoder unit/test_decoder.cpp)
te_add_test(test_bitstamp_snapshot unit/test_bitstamp_snapshot.cpp)
te_add_test(test_bitstamp_classifier unit/test_bitstamp_classifier.cpp)
te_add_test(test_price_level unit/test_price_level.cpp)
te_add_test(test_order_book unit/test_order_book.cpp)
te_add_test(test_book_oracle unit/test_book_oracle.cpp)
te_add_test(test_bitstamp_bootstrap unit/test_bitstamp_bootstrap.cpp)
te_add_test(test_bitstamp_replay unit/test_bitstamp_replay.cpp)
te_add_test(test_golden_replay unit/test_golden_replay.cpp)
te_add_test(test_trade_reconciler unit/test_trade_reconciler.cpp)
te_add_test(test_bitstamp_trade_decoder unit/test_bitstamp_trade_decoder.cpp)
te_add_test(test_time unit/test_time.cpp)
te_add_test(test_byte_buffer unit/test_byte_buffer.cpp)
te_add_test(test_record unit/test_record.cpp)
te_add_test(test_record_roundtrip unit/test_record_roundtrip.cpp)
te_add_test(test_recorder unit/test_recorder.cpp)
te_add_test(test_bitstamp_joined_capture unit/test_bitstamp_joined_capture.cpp)
te_add_test(test_segment_loader unit/test_segment_loader.cpp)
te_add_test(test_manifest_reader unit/test_manifest_reader.cpp)
te_add_test(test_capture_coordinator unit/test_capture_coordinator.cpp)
te_add_test(test_book_health unit/test_book_health.cpp)
te_add_test(test_segment_format unit/test_segment_format.cpp)
te_add_test(test_event_segment_writer unit/test_event_segment_writer.cpp)
