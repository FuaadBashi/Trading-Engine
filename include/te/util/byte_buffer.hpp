#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Slice 1. Read/write primitives over a byte span for the binary capture.

namespace te {

/**
 * @brief  Writes one byte into @p buffer at @p offset.
 *
 * @param  buffer Destination bytes. Not resized; only existing bytes are written.
 * @param  offset Position from the start of @p buffer, in bytes.
 * @param  value  The byte to store.
 *
 * @return True on success; false if the write would run past the end of @p buffer, in which
 *         case no byte is modified.
 *
 * @note   Returns bool rather than a Result because there is exactly one way to fail, per
 *         ADR 0003.
 */
bool writeU8(std::span<std::byte> buffer, std::size_t offset, std::uint8_t value);

/**
 * @brief  Reads one byte out of @p buffer at @p offset.
 *
 * @param  buffer Source bytes.
 * @param  offset Position from the start of @p buffer, in bytes.
 * @param  out    Receives the byte on success; untouched on failure.
 *
 * @return True on success; false if the read would run past the end of @p buffer.
 */
bool readU8(std::span<const std::byte> buffer, std::size_t offset, std::uint8_t& out);

/**
 * @brief  Writes eight bytes into @p buffer at @p offset.
 *
 * @param  buffer Destination bytes. Not resized; only existing bytes are written.
 * @param  offset Position from the start of @p buffer, in bytes. Need not be 8-byte aligned.
 * @param  value  The value to store.
 *
 * @return True on success; false if the write would run past the end of @p buffer, in which
 *         case no byte is modified.
 *
 * @note   Copies via std::memcpy rather than casting: a cast between std::byte and
 *         std::uint64_t only ever moves one byte, and memcpy is also the standards-safe way
 *         to handle an @p offset that is not 8-byte aligned, which a packed record layout
 *         routinely produces.
 *
 * @note   Byte order is the host's. The capture format is therefore currently defined
 *         relative to the machine that wrote it; fixing it explicitly is deferred until a
 *         second architecture actually needs to read a capture.
 */
bool writeU64(std::span<std::byte> buffer, std::size_t offset, std::uint64_t value);

/**
 * @brief  Reads eight bytes out of @p buffer at @p offset.
 *
 * @param  buffer Source bytes.
 * @param  offset Position from the start of @p buffer, in bytes. Need not be 8-byte aligned.
 * @param  out    Receives the value on success; untouched on failure.
 *
 * @return True on success; false if the read would run past the end of @p buffer.
 */
bool readU64(std::span<const std::byte> buffer, std::size_t offset, std::uint64_t& out);

}  // namespace te
