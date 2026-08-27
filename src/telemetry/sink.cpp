#include <te/telemetry/sink.hpp>
#include <utility>

namespace te {

Sink::Sink(std::ofstream file) : file_{std::move(file)} {}

Result<Sink, SinkError> Sink::open(const std::string& path) {
    // app: every write goes to the current end of file. binary: no text-mode translation,
    // which would corrupt a fixed-size record layout.
    std::ofstream file(path, std::ios::app | std::ios::binary);

    if (!file.is_open()) {
        return Result<Sink, SinkError>::failure(SinkError::cannot_open);
    }

    return Result<Sink, SinkError>::success(Sink(std::move(file)));
}

bool Sink::write(const Record& record) {
    // Valid because Record is trivially copyable (static_assert in record.hpp) and because
    // accessing any object's bytes through a char* is explicitly permitted by the standard.
    file_.write(reinterpret_cast<const char*>(&record), sizeof(record));
    return file_.good();
}

bool Sink::flush() {
    file_.flush();
    return file_.good();
}

bool Sink::ok() const { return file_.good(); }

}  // namespace te
