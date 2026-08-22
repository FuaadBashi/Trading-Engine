import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts.validate_capture import validate_manifest as validate_any_capture
from scripts.validate_joined_capture import validate_manifest


ORDER_CHANNEL = "live_orders_btcusd"
TRADE_CHANNEL = "live_trades_btcusd"
RUN_ID = "joined-capture-test-run"
FIRST_EVENT_ID = "00000000-0000-0000-0000-000000000001"
PRE_EVENT_ID = "00000000-0000-0000-0000-000000000000"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class JoinedCaptureValidatorTest(unittest.TestCase):
    def create_capture(self) -> tuple[tempfile.TemporaryDirectory, Path]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        snapshot = {
            "microtimestamp": "100",
            "bids": [],
            "asks": [],
        }
        snapshot_bytes = json.dumps(snapshot).encode("utf-8")
        (root / "segment-0000.snapshot").write_bytes(snapshot_bytes)
        checkpoint = {
            "microtimestamp": "200",
            "bids": [],
            "asks": [],
        }
        checkpoint_bytes = json.dumps(checkpoint).encode("utf-8")
        (root / "checkpoint-0000.snapshot").write_bytes(checkpoint_bytes)

        payloads = [
            {"event": "bts:subscription_succeeded", "channel": ORDER_CHANNEL, "data": {}},
            {"event": "bts:subscription_succeeded", "channel": TRADE_CHANNEL, "data": {}},
            {
                "event": "order_created",
                "channel": ORDER_CHANNEL,
                "event_id": FIRST_EVENT_ID,
                "pre_event_id": PRE_EVENT_ID,
                "data": {"microtimestamp": "101"},
            },
            {
                "event": "trade",
                "channel": TRADE_CHANNEL,
                "data": {"microtimestamp": "102"},
            },
        ]
        raw_lines = [json.dumps(payload, separators=(",", ":")) + "\n" for payload in payloads]
        stream_kinds = ["control", "control", "order", "trade"]
        venue_timestamps = [None, None, "101", "102"]
        frames = [
            {
                "captureOrdinal": index,
                "localWallTimestampNanos": index,
                "localSteadyTimestampNanos": index,
                "streamKind": stream_kinds[index - 1],
                "venueTimestampMicros": venue_timestamps[index - 1],
                "runId": RUN_ID,
                "segmentId": 0,
                "payloadLine": index,
            }
            for index in range(1, len(raw_lines) + 1)
        ]
        frame_lines = [json.dumps(frame, separators=(",", ":")) + "\n" for frame in frames]
        raw_bytes = "".join(raw_lines).encode("utf-8")
        frame_bytes = "".join(frame_lines).encode("utf-8")
        (root / "segment-0000.jsonl").write_bytes(raw_bytes)
        (root / "segment-0000.frames.jsonl").write_bytes(frame_bytes)

        manifest = {
            "format_version": 2,
            "capture_format": "bitstamp_joined_frames_v1",
            "venue": "bitstamp",
            "instrument": "btcusd",
            "channels": [ORDER_CHANNEL, TRADE_CHANNEL],
            "runId": RUN_ID,
            "status": "completed",
            "lastCaptureOrdinal": 4,
            "segments": [
                {
                    "index": 0,
                    "payload": "segment-0000.jsonl",
                    "frame_index": "segment-0000.frames.jsonl",
                    "snapshot": "segment-0000.snapshot",
                    "checkpoint": "checkpoint-0000.snapshot",
                    "snapshot_metadata": {
                        "microtimestamp": "100",
                        "bytes": len(snapshot_bytes),
                        "sha256": sha256(snapshot_bytes),
                        "requested_at": "2026-01-01T00:00:00.000000Z",
                        "received_at": "2026-01-01T00:00:00.100000Z",
                    },
                    "checkpoint_metadata": {
                        "microtimestamp": "200",
                        "bytes": len(checkpoint_bytes),
                        "sha256": sha256(checkpoint_bytes),
                        "requested_at": "2026-01-01T00:01:00.000000Z",
                        "received_at": "2026-01-01T00:01:00.100000Z",
                        "capture_ordinal_before_request": 4,
                        "capture_ordinal_at_receive": 4,
                        "capture_ordinal_after_drain": 4,
                        "post_checkpoint_drain_seconds": 5.0,
                        "drain_completed_at": "2026-01-01T00:01:05.100000Z",
                    },
                    "frames": 4,
                    "order_events": 1,
                    "trade_events": 1,
                    "control_frames": 2,
                    "payload_bytes": len(raw_bytes),
                    "payload_sha256": sha256(raw_bytes),
                    "frames_bytes": len(frame_bytes),
                    "frames_sha256": sha256(frame_bytes),
                    "first_event_id": FIRST_EVENT_ID,
                    "last_event_id": FIRST_EVENT_ID,
                    "first_order_microtimestamp": "101",
                    "last_order_microtimestamp": "101",
                    "first_trade_microtimestamp": "102",
                    "last_trade_microtimestamp": "102",
                    "first_capture_ordinal": 1,
                    "last_capture_ordinal": 4,
                    "subscriptions": [ORDER_CHANNEL, TRADE_CHANNEL],
                    "chain_valid": True,
                    "end_reason": "graceful_checkpoint",
                }
            ],
        }
        manifest_path = root / "manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return temporary, manifest_path

    def test_accepts_a_consistent_joined_capture(self):
        temporary, manifest_path = self.create_capture()
        self.addCleanup(temporary.cleanup)

        self.assertEqual(validate_manifest(manifest_path), 0)
        self.assertEqual(validate_any_capture(manifest_path), 0)

    def test_rejects_a_non_monotonic_capture_ordinal(self):
        temporary, manifest_path = self.create_capture()
        self.addCleanup(temporary.cleanup)
        root = manifest_path.parent
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        frames_path = root / "segment-0000.frames.jsonl"
        frames = [json.loads(line) for line in frames_path.read_text(encoding="utf-8").splitlines()]
        frames[2]["captureOrdinal"] = 9
        frame_bytes = (
            "".join(json.dumps(frame, separators=(",", ":")) + "\n" for frame in frames)
        ).encode("utf-8")
        frames_path.write_bytes(frame_bytes)
        manifest["segments"][0]["frames_bytes"] = len(frame_bytes)
        manifest["segments"][0]["frames_sha256"] = sha256(frame_bytes)
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        self.assertEqual(validate_manifest(manifest_path), 1)

    def test_rejects_a_checkpoint_that_does_not_match_its_manifest(self):
        temporary, manifest_path = self.create_capture()
        self.addCleanup(temporary.cleanup)
        checkpoint_path = manifest_path.parent / "checkpoint-0000.snapshot"
        checkpoint_path.write_text(
            json.dumps({"microtimestamp": "99", "bids": [], "asks": []}),
            encoding="utf-8",
        )

        self.assertEqual(validate_manifest(manifest_path), 1)


if __name__ == "__main__":
    unittest.main()
