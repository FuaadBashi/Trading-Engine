#pragma once

#include <fstream>
#include <string>

#include <te/core/result.hpp>
#include <te/telemetry/record.hpp>

// Slice 1. Append-only file sink now, ZeroMQ sink in slice 5.

namespace te {

/**
 * @brief  Reason a Sink could not be opened, written to, or flushed.
 */
enum class SinkError {
    /**
     * @brief  The file could not be opened for appending.
     *
     * @note   Bad path, missing parent directory, insufficient permissions, or the path names
     *         a directory rather than a file.
     */
    cannot_open,

    /**
     * @brief  A write or flush failed after the file had opened successfully.
     *
     * @note   Typically a full disk or a device error. The underlying stream is left in a
     *         failed state, so every subsequent write on that Sink also fails; see ok().
     */
    write_failed,
};

/**
 * @brief  Append-only binary writer for captured Records.
 *
 * @note   "Sink" is the standard term for the destination an event stream is written to;
 *         spdlog and Serilog both use it. This is the write side of the capture pipeline that
 *         decodeBitstampEvent and buildRecord feed.
 *
 * @note   Opened with std::ios::app, so every write lands at the current end of the file
 *         regardless of what wrote there previously. On POSIX this maps to O_APPEND, whose
 *         single-write atomicity is the property real append-only logs -- syslog, journald,
 *         database write-ahead logs -- are built on. std::ios::binary is non-negotiable for a
 *         fixed-size record format: text-mode translation would corrupt the bytes.
 *
 * @note   A record is written as a plain memcpy of its bytes, valid only because Record is
 *         trivially copyable, which record.hpp static_asserts. Accessing any object's bytes
 *         through a char* is the one aliasing case the standard explicitly permits, so this
 *         is well-defined rather than merely working in practice.
 *
 * @note   Ownership is RAII: the file is opened once by open() and closed automatically when
 *         the Sink is destroyed, so an early return cannot leak a handle. Movable but not
 *         copyable -- two Sinks appending through one handle has no sensible meaning.
 *
 * @note   Holds no flush policy. Deciding when to flush (every record, every N records, on a
 *         timer) is the caller's call, the same way InstrumentSpec's scales are supplied by
 *         the caller rather than guessed at by the decoder.
 */
class Sink {
public:
    /**
     * @brief  Opens @p path for binary appending, creating the file if it does not exist.
     *
     * @param  path Filesystem path to the capture file.
     *
     * @return An open Sink, or SinkError::cannot_open.
     *
     * @note   Returns a Result rather than throwing, per ADR 0002. There is no
     *         default-constructed "closed" Sink, so a Sink that exists is always one that
     *         opened successfully.
     *
     * @note   The Sink lives inside the returned Result and must be used in place or moved
     *         out; Result::valueIf() has a non-const overload for exactly this case.
     */
    static Result<Sink, SinkError> open(const std::string& path);

    /**
     * @brief  Appends one record's raw bytes to the file.
     *
     * @param  record The record to append.
     *
     * @return True if the bytes were accepted; false if the underlying stream has failed.
     *
     * @note   Buffering is the stream's. A true return means the bytes were accepted, not
     *         that they have reached the disk; call flush() for that.
     *
     * @note   Returns bool rather than a Result because there is exactly one way to fail,
     *         per ADR 0003.
     */
    bool write(const Record& record);

    /**
     * @brief  Pushes buffered bytes to the operating system.
     *
     * @return True on success; false if the stream has failed.
     *
     * @note   Not the same as durability. The OS may still hold the data in its page cache:
     *         a crash of this process cannot lose flushed bytes, but a machine power loss
     *         still can. Full durability needs fsync, which std::ofstream does not expose --
     *         out of scope while the capture is a local file.
     */
    bool flush();

    /**
     * @brief  Reports whether the underlying stream is still healthy.
     *
     * @return True while writes may still succeed.
     *
     * @note   Once a write fails the stream stays in a failed state, so every later write on
     *         this Sink fails too.
     */
    bool ok() const;

    Sink(Sink&&) = default;
    Sink& operator=(Sink&&) = default;
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;

private:
    /**
     * @brief  Adopts an already-open stream.
     *
     * @param  file The opened output stream, moved in.
     *
     * @note   Private, so a Sink may only come from open() and an unopened Sink is
     *         unrepresentable.
     */
    explicit Sink(std::ofstream file);

    std::ofstream file_;
};

}  // namespace te
