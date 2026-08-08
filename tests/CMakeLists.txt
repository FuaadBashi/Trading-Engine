# Uncomment once googletest is enabled in cmake/dependencies.cmake.

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
