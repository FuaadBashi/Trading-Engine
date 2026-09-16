#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "te/core/sha256.hpp"

namespace {

std::vector<std::byte> asBytes(const std::string& text) {
    std::vector<std::byte> bytes(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[i] = static_cast<std::byte>(text[i]);
    }
    return bytes;
}

}  // namespace

// Every expected value below is the real SHA-256 digest of the given input (verified against
// Python's hashlib before this test was written). The 55/56/64-byte cases sit exactly on the
// padding boundary where a message needs a second 64-byte block -- the classic place for an
// off-by-one bug in a hand-written implementation.

TEST(Sha256, EmptyInput) {
    EXPECT_EQ(te::sha256Hex({}),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, KnownVectorAbc) {
    const std::vector<std::byte> data = asBytes("abc");
    EXPECT_EQ(te::sha256Hex(data),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, KnownVectorQuickBrownFox) {
    const std::vector<std::byte> data = asBytes("The quick brown fox jumps over the lazy dog");
    EXPECT_EQ(te::sha256Hex(data),
              "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST(Sha256, FiftyFiveByteMessageFitsPaddingInOneBlock) {
    const std::vector<std::byte> data(55, std::byte{'a'});
    EXPECT_EQ(te::sha256Hex(data),
              "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
}

TEST(Sha256, FiftySixByteMessageNeedsASecondBlockForPadding) {
    const std::vector<std::byte> data(56, std::byte{'a'});
    EXPECT_EQ(te::sha256Hex(data),
              "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
}

TEST(Sha256, ExactlyOneFullBlockStillNeedsASecondBlockForPadding) {
    const std::vector<std::byte> data(64, std::byte{'a'});
    EXPECT_EQ(te::sha256Hex(data),
              "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST(Sha256, MultiBlockInputProcessesEveryBlock) {
    std::vector<std::byte> data;
    data.reserve(1024);
    for (int rep = 0; rep < 4; ++rep) {
        for (int byte = 0; byte < 256; ++byte) {
            data.push_back(static_cast<std::byte>(byte));
        }
    }
    ASSERT_EQ(data.size(), 1024U);
    EXPECT_EQ(te::sha256Hex(data),
              "785b0751fc2c53dc14a4ce3d800e69ef9ce1009eb327ccf458afe09c242c26c9");
}

TEST(Sha256, DifferentInputsProduceDifferentDigests) {
    const std::vector<std::byte> a = asBytes("abc");
    const std::vector<std::byte> b = asBytes("abd");
    EXPECT_NE(te::sha256Hex(a), te::sha256Hex(b));
}
