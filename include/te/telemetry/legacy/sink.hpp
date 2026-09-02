#pragma once

#include <fstream>
#include <string>
#include <te/core/result.hpp>
#include <te/telemetry/legacy/record.hpp>

namespace te {

enum class SinkError {
    cannot_open,
    write_failed,
};

// RAII append-only binary writer for legacy Record bytes. Movable, not copyable; the caller owns
// flush policy. write() means accepted by the stream, while flush() still is not fsync durability.
class Sink {
public:
    // No public default constructor: an existing Sink always adopted an opened stream.
    static Result<Sink, SinkError> open(const std::string& path);

    bool write(const Record& record);
    bool flush();
    bool ok() const;

    Sink(Sink&&) = default;
    Sink& operator=(Sink&&) = default;
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;

private:
    explicit Sink(std::ofstream file);

    std::ofstream file_;
};

}  // namespace te
