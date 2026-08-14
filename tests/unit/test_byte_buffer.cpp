#include <gtest/gtest.h>

#include <vector>

#include <te/util/byte_buffer.hpp>

// ---- round-trip tests

TEST(ByteBuffer, WriteAndReadU8RoundTrips) {
    std::vector<std::byte> storage(4, std::byte{0});
    std::span<std::byte> buffer(storage);

    ASSERT_TRUE(te::writeU8(buffer, 0, 0x42));

    std::uint8_t out = 0;
    ASSERT_TRUE(te::readU8(buffer, 0, out));
    EXPECT_EQ(out, 0x42);
}

TEST(ByteBuffer, WriteAndReadU64RoundTrips) {
    std::vector<std::byte> storage(8, std::byte{0});
    std::span<std::byte> buffer(storage);

    const std::uint64_t value = 0x0102030405060708ULL;
    ASSERT_TRUE(te::writeU64(buffer, 0, value));

    std::uint64_t out = 0;
    ASSERT_TRUE(te::readU64(buffer, 0, out));
    EXPECT_EQ(out, value);
}

TEST(ByteBuffer, WriteAndReadU64RoundTripsMidBuffer) {
    std::vector<std::byte> storage(64, std::byte{0xFF});
    std::span<std::byte> buffer(storage);

    const std::uint64_t value = 0xAABBCCDDEEFF0011ULL;
    ASSERT_TRUE(te::writeU64(buffer, 10, value));

    std::uint64_t out = 0;
    ASSERT_TRUE(te::readU64(buffer, 10, out));
    EXPECT_EQ(out, value);
}

// ---- bounds: exact-fit boundary 

TEST(ByteBuffer, AcceptsU8WriteAtLastValidByte) {
    std::vector<std::byte> storage(4, std::byte{0});
    std::span<std::byte> buffer(storage);

    EXPECT_TRUE(te::writeU8(buffer, 3, 0x99));
}

TEST(ByteBuffer, AcceptsU64WriteInExactFitBuffer) {
    std::vector<std::byte> storage(8, std::byte{0});
    std::span<std::byte> buffer(storage);

    EXPECT_TRUE(te::writeU64(buffer, 0, 0x1122334455667788ULL));
}

// ---- bounds: rejection

TEST(ByteBuffer, RejectsU8WriteOnePastTheEnd) {
    std::vector<std::byte> storage(4, std::byte{0});
    std::span<std::byte> buffer(storage);

    EXPECT_FALSE(te::writeU8(buffer, 4, 0x01));
}

TEST(ByteBuffer, RejectsU64WriteWhenBufferIsOneByteTooSmall) {
    std::vector<std::byte> storage(7, std::byte{0});
    std::span<std::byte> buffer(storage);

    EXPECT_FALSE(te::writeU64(buffer, 0, 42));
}

TEST(ByteBuffer, RejectsU64ReadWhenNotEnoughRoomRemains) {
    std::vector<std::byte> storage(4, std::byte{0});
    std::span<const std::byte> buffer(storage);

    std::uint64_t out = 0;
    EXPECT_FALSE(te::readU64(buffer, 3, out));
}

// ---- proves a write doesn't corrupt memory outside its own range

TEST(ByteBuffer, U64WriteDoesNotTouchAdjacentBytes) {
    std::vector<std::byte> storage(64, std::byte{0xFF});
    std::span<std::byte> buffer(storage);

    ASSERT_TRUE(te::writeU64(buffer, 10, 0xAABBCCDDEEFF0011ULL));

    EXPECT_EQ(storage[9], std::byte{0xFF});
    EXPECT_EQ(storage[18], std::byte{0xFF});
}
