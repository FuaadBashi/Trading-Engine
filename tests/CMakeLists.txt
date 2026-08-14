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
te_add_test(test_time unit/test_time.cpp)
te_add_test(test_byte_buffer unit/test_byte_buffer.cpp)
