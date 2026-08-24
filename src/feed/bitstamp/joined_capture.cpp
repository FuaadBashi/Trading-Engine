#include <te/feed/bitstamp/joined_capture.hpp>
#include <te/core/result.hpp>
#include "simdjson/padded_string.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string_view.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/ondemand.h"
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>


namespace te::bitstamp {
    //   "payload": "segment-0000.jsonl",
    //   "frame_index": "segment-0000.frames.jsonl",
    //   "snapshot": "segment-0000.snapshot",
    //   "checkpoint": "checkpoint-0000.snapshot",
    //   "started_at": "2026-08-22T00:05:12.783006Z",

    Result<BookSnapshot, JoinedCaptureError> loadSnapshot(const std::filesystem::path& snapshotPath, InstrumentSpec spec, JoinedCaptureError unreadableError, 
                                                                                                                        JoinedCaptureError parseError)
    {

        std::ifstream snapshotInput(snapshotPath,std::ios::binary);
        if (!snapshotInput) {
            // return payload_unreadable
            return Result<BookSnapshot, JoinedCaptureError>::failure(unreadableError);
        }
        std::ostringstream snapshotContent;
        snapshotContent << snapshotInput.rdbuf();
        Result<BookSnapshot, SnapshotError> snapshot_parsed = parseSnapshot(snapshotContent.str(), spec);
        if (snapshot_parsed.valueIf() == nullptr) {
            return Result<BookSnapshot, JoinedCaptureError>::failure(parseError);

        } else {
            return Result<BookSnapshot, JoinedCaptureError>::success(*snapshot_parsed.valueIf());
        }

    };


    Result<JoinedCapture, JoinedCaptureError> loadJoinedCapture(const std::filesystem::path& captureDirectory, InstrumentSpec spec){
        // 1. Read manifest. [x]
        // 2. Find S0, S1, raw JSONL, and frame-index JSONL. [x]
        // 3. Parse S0 and S1 using parseSnapshot. [x]
        // 4. Open raw and frame files.[]
        // 5. Read them in pairs until both end.
        // 6. Reject if only one file has a next line.
        // 7. Parse frame JSON.
        // 8. Check streamKind.
        // 9. order → decodeEvent → append orderEvents.
        // 10. trade → decodeTrade → append tradeEvents.
        // 11. control → skip.
        // 12. Return the completed JoinedCapture.

        // If captureDirectory is data/raw/bitstamp-btcusd-20260822T000512Z,
        // manifestPath becomes
        // data/raw/bitstamp-btcusd-20260822T000512Z/manifest.json.
        const std::filesystem::path manifestPath = captureDirectory / "manifest.json";
        JoinedCapture joinCapture;
        

        std::ifstream input(manifestPath, std::ios::binary);
        if (!input) {
            return  Result<JoinedCapture, JoinedCaptureError>::failure(JoinedCaptureError::manifest_unreadable);
        }

        std::ostringstream contents;
        contents << input.rdbuf();
        std::string manifestText = contents.str();

        simdjson::ondemand::parser parser;
        simdjson::padded_string buffer = simdjson::padded_string(manifestText);

        simdjson::ondemand::document doc;
        simdjson::error_code err = parser.iterate(buffer).get(doc);
        if (err) {
            // couldn't even parse this as JSON at all
            return  Result<JoinedCapture, JoinedCaptureError>::failure(JoinedCaptureError::manifest_malformed);
        }

        simdjson::ondemand::array segments;

        err = doc["segments"].get_array().get(segments);
        if (err) {
            // "segments" missing or not an array
            return  Result<JoinedCapture, JoinedCaptureError>::failure(JoinedCaptureError::manifest_missing_field);
        }

        bool foundSegment = false;
        std::string_view payload;
        std::string_view frame_index;
        std::string_view snapshot;
        std::string_view checkpoint;

        for (auto segmentResult : segments) {
            simdjson::ondemand::object segment;

            err = segmentResult.get_object().get(segment);
            if (err) {
                // array item is not { ... }
                return  Result<JoinedCapture, JoinedCaptureError>::failure(JoinedCaptureError::manifest_missing_field);
            }
            err = segment["payload"].get_string().get(payload);
            if (err) {
                // no payload" field at all
                return  Result<JoinedCapture, JoinedCaptureError>::failure(JoinedCaptureError::manifest_missing_field);
            }
            err = segment["frame_index"].get_string().get(frame_index);
            if (err) {
                // no payload" field at all
                return  Result<JoinedCapture, JoinedCaptureError>::failure(JoinedCaptureError::manifest_missing_field);
            }

            err = segment["snapshot"].get_string().get(snapshot);
            if (err) {
                // no payload" field at all
                return  Result<JoinedCapture, JoinedCaptureError>::failure(JoinedCaptureError::manifest_missing_field);
            }
        
            err = segment["checkpoint"].get_string().get(checkpoint);
            if (err) {
                // no payload" field at all
                return  Result<JoinedCapture, JoinedCaptureError>::failure(JoinedCaptureError::manifest_missing_field);
            }
            foundSegment = true;
            break;
        }
        if (!foundSegment) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(JoinedCaptureError::manifest_missing_segment);

        }

        const std::filesystem::path payloadPath = captureDirectory / std::filesystem::path{std::string{payload}};

        const std::filesystem::path frameIndexPath = captureDirectory / std::filesystem::path{std::string{frame_index}};

        const std::filesystem::path seedPath = captureDirectory / std::filesystem::path{std::string{snapshot}};

        const std::filesystem::path checkpointPath = captureDirectory / std::filesystem::path{std::string{checkpoint}};
   


        Result<BookSnapshot, JoinedCaptureError> seed_snapshot =  loadSnapshot(seedPath,  spec, JoinedCaptureError::seed_unreadable, JoinedCaptureError::seed_cant_be_parsed);
        Result<BookSnapshot, JoinedCaptureError> checkpoint_snapshot =  loadSnapshot(checkpointPath, spec, JoinedCaptureError::checkpoint_unreadable, JoinedCaptureError::checkpoint_cant_be_parsed);

        if (seed_snapshot.hasValue() && checkpoint_snapshot.hasValue()){
            joinCapture.seed = *seed_snapshot.valueIf();
            joinCapture.checkpoint = *checkpoint_snapshot.valueIf();
        } else if (!seed_snapshot.hasValue()) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(*seed_snapshot.errorIf());
        } else {
            return Result<JoinedCapture, JoinedCaptureError>::failure(*checkpoint_snapshot.errorIf());
        }



        return  Result<JoinedCapture, JoinedCaptureError>::success(joinCapture);

    }
}  // namespace te::bitstamp
