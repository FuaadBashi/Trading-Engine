# Unit-test target convention: compile one test source, link project code plus GoogleTest, then
# let CMake discover every TEST(Suite, Name) case for CTest.

include(GoogleTest)

# CTest labels, so `ctest -L <label>` can select a category (plan v4 §21). A label applies to
# every case in a binary: gtest_discover_tests has no finer granularity, so a binary that mixes
# ordinary assertions with one real-corpus-only case (see GTEST_SKIP below) carries both labels
# rather than pretending to a precision this setup does not have.
#
#   unit   - synthetic inputs only, always runs, no private data.
#   golden - asserts against a committed fixture under tests/fixtures/; byte- or state-exact,
#            catches regressions no one thought to unit test.
#   corpus - contains a case that skips via GTEST_SKIP when private real-venue capture data is
#            absent (gitignored `data/`). Never the only evidence for a gate; see the golden
#            fixture each of these falls back to.
#
# No `sanitizer` or `benchmark` label yet: sanitizers are a build-type switch (Debug ASan/UBSan),
# not a separate test binary, and no benchmark binaries exist until Stage 8. Add the labels when
# something real would carry them, not before.
function(te_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE te_core GTest::gtest_main)
    te_set_warnings(${name})
    te_enable_sanitizers(${name})
    # PROPERTIES forwards LABELS through a generated script; an unescaped ";" is read back as
    # a space, so gtest_discover_tests keeps only the first label of a multi-label list and
    # silently drops the rest. "\;" survives that extra layer of expansion intact.
    list(JOIN ARGN "\\;" te_test_labels)
    gtest_discover_tests(${name}
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "${te_test_labels}"
    )
endfunction()

te_add_test(test_types unit/test_types.cpp unit)   # start here: one trivial assertion, green in CI
te_add_test(test_events unit/test_events.cpp unit)
te_add_test(test_instrument unit/test_instrument.cpp unit)
te_add_test(test_result unit/test_result.cpp unit)
te_add_test(test_decimal unit/test_decimal.cpp unit)
te_add_test(test_decoder unit/test_decoder.cpp unit)
te_add_test(test_bitstamp_snapshot unit/test_bitstamp_snapshot.cpp unit corpus)
te_add_test(test_bitstamp_classifier unit/test_bitstamp_classifier.cpp unit)
te_add_test(test_price_level unit/test_price_level.cpp unit)
te_add_test(test_order_book unit/test_order_book.cpp unit)
te_add_test(test_book_oracle unit/test_book_oracle.cpp unit)
te_add_test(test_bitstamp_bootstrap unit/test_bitstamp_bootstrap.cpp unit)
te_add_test(test_bitstamp_replay unit/test_bitstamp_replay.cpp unit)
te_add_test(test_golden_replay unit/test_golden_replay.cpp golden corpus)
te_add_test(test_trade_reconciler unit/test_trade_reconciler.cpp unit)
te_add_test(test_bitstamp_trade_decoder unit/test_bitstamp_trade_decoder.cpp unit)
te_add_test(test_time unit/test_time.cpp unit)
te_add_test(test_byte_buffer unit/test_byte_buffer.cpp unit)
te_add_test(test_record unit/test_record.cpp unit)
te_add_test(test_record_roundtrip unit/test_record_roundtrip.cpp golden)
te_add_test(test_recorder unit/test_recorder.cpp golden corpus)
te_add_test(test_bitstamp_joined_capture unit/test_bitstamp_joined_capture.cpp golden corpus)
te_add_test(test_segment_loader unit/test_segment_loader.cpp unit)
te_add_test(test_manifest_reader unit/test_manifest_reader.cpp unit)
te_add_test(test_capture_coordinator unit/test_capture_coordinator.cpp unit)
te_add_test(test_merge_cursor unit/test_merge_cursor.cpp unit)
te_add_test(test_event_tape_writer unit/test_event_tape_writer.cpp unit)
te_add_test(test_book_health unit/test_book_health.cpp unit)
te_add_test(test_segment_format unit/test_segment_format.cpp unit)
te_add_test(test_event_segment_reader unit/test_event_segment_reader.cpp unit)
te_add_test(test_event_segment_writer unit/test_event_segment_writer.cpp unit)
