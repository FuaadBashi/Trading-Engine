cmake_minimum_required(VERSION 3.25)
cmake_policy(SET CMP0135 NEW)   # FetchContent URL downloads get sane timestamps

project(TradingEngine VERSION 0.1.0 LANGUAGES CXX)

# ==============================================================================
# Configuration & Language Standards
# ==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to RelWithDebInfo. A latency number from a Debug build is meaningless,
# and a profile without symbols is unreadable. Never quote numbers from Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Expose compile_commands.json to root for clangd / language servers
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/compile_commands.json
                ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Exposing compile_commands.json to clangd")
endif()

include(cmake/warnings.cmake)
include(cmake/sanitizers.cmake)
include(cmake/dependencies.cmake)

# ==============================================================================
# Core library — every line of engine code lives here. No main() ever.
# ==============================================================================
add_library(te_core)

target_include_directories(te_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_sources(te_core PRIVATE
    # Slice 1
    src/core/text_to_int.cpp
    src/core/time.cpp
    src/util/byte_buffer.cpp
    # src/core/types.cpp
    src/telemetry/record.cpp
    src/telemetry/recorder.cpp
    src/telemetry/sink.cpp
    src/feed/bitstamp_decoder.cpp
    #
    # Slice 2
    src/book/order_book.cpp
    src/book/price_level.cpp
    src/feed/bitstamp_snapshot.cpp
    src/util/intrusive_list.cpp
    # memory_pool.hpp is a template (Pool<T, N>); header-only, like result.hpp -- no .cpp to add.
    #
    # Slice 3
    # src/engine/engine.cpp
    # src/engine/portfolio.cpp
    # src/feed/replay_feed.cpp
    # src/venue/simulated_venue.cpp
)


target_link_libraries(te_core PUBLIC Threads::Threads simdjson)
te_set_warnings(te_core)
te_enable_sanitizers(te_core)

target_compile_definitions(te_core PUBLIC
    TE_PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    TE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)

# ==============================================================================
# Apps — thin main() files. If one grows past ~50 lines, it belongs in te_core.
# ==============================================================================
function(te_add_app name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE te_core)
    te_set_warnings(${name})
    te_enable_sanitizers(${name})
endfunction()

te_add_app(recorder apps/recorder_main.cpp)

set_target_properties(recorder PROPERTIES OUTPUT_NAME tep)
# te_add_app(replay apps/replay_main.cpp)   # slice 3
# te_add_app(live   apps/live_main.cpp)     # slice 5

# ==============================================================================
# Tests
# ==============================================================================
enable_testing()
add_subdirectory(tests)
