import asyncio
import hashlib
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

websockets_module = types.ModuleType("websockets")
websockets_asyncio_module = types.ModuleType("websockets.asyncio")
websockets_client_module = types.ModuleType("websockets.asyncio.client")
websockets_client_module.connect = None
sys.modules.setdefault("websockets", websockets_module)
sys.modules.setdefault("websockets.asyncio", websockets_asyncio_module)
sys.modules.setdefault("websockets.asyncio.client", websockets_client_module)

from scripts import dump_raw_ws_bitstamp as capture


ORDER_EVENT_ID = "00000000-0000-0000-0000-000000000001"
PRE_ORDER_EVENT_ID = "00000000-0000-0000-0000-000000000000"


class FakeWebSocket:
    def __init__(self, messages):
        self.messages = list(messages)
        self.wait_forever = asyncio.Event()

    async def send(self, _message):
        return None

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.messages:
            await asyncio.sleep(0)
            return self.messages.pop(0)
        await self.wait_forever.wait()
        raise StopAsyncIteration


class FakeConnection:
    def __init__(self, websocket):
        self.websocket = websocket

    async def __aenter__(self):
        return self.websocket

    async def __aexit__(self, _exception_type, _exception, _traceback):
        return False


class CaptureCheckpointTest(unittest.IsolatedAsyncioTestCase):
    async def test_graceful_stop_writes_a_later_checkpoint_and_drains_reader(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)

        payloads = [
            {
                "event": "bts:subscription_succeeded",
                "channel": capture.ORDER_CHANNEL,
                "data": {},
            },
            {
                "event": "bts:subscription_succeeded",
                "channel": capture.TRADE_CHANNEL,
                "data": {},
            },
            {
                "event": "order_created",
                "channel": capture.ORDER_CHANNEL,
                "event_id": ORDER_EVENT_ID,
                "pre_event_id": PRE_ORDER_EVENT_ID,
                "data": {"microtimestamp": "101"},
            },
            {
                "event": "trade",
                "channel": capture.TRADE_CHANNEL,
                "data": {"microtimestamp": "102"},
            },
        ]
        websocket = FakeWebSocket(
            json.dumps(payload, separators=(",", ":")) for payload in payloads
        )

        def fake_fetch_snapshot(path):
            # The seed snapshot must be at least as new as the first captured order event (101),
            # otherwise the window between them is covered by neither source and the segment has
            # no valid seed. See test_stale_snapshot_is_refetched_until_it_overlaps_the_stream.
            microtimestamp = "200" if path.name.startswith("checkpoint-") else "150"
            body = json.dumps(
                {"microtimestamp": microtimestamp, "bids": [], "asks": []}
            ).encode("utf-8")
            path.write_bytes(body)
            return {
                "bytes": len(body),
                "sha256": hashlib.sha256(body).hexdigest(),
                "microtimestamp": microtimestamp,
                "requested_at": "2026-01-01T00:00:00.000000Z",
                "received_at": "2026-01-01T00:00:00.100000Z",
            }

        segment = {
            "index": 0,
            "payload": "segment-0000.jsonl",
            "frame_index": "segment-0000.frames.jsonl",
            "snapshot": "segment-0000.snapshot",
            "checkpoint": "checkpoint-0000.snapshot",
        }
        manifest = {
            "runId": "checkpoint-test-run",
            "lastCaptureOrdinal": 0,
            "segments": [segment],
        }
        manifest_path = root / "manifest.json"
        capture.write_manifest(manifest_path, manifest)
        stop_requested = asyncio.Event()
        stop_requested.set()

        with (
            patch.object(capture, "connect", return_value=FakeConnection(websocket)),
            patch.object(capture, "fetch_snapshot", side_effect=fake_fetch_snapshot),
            patch.object(capture, "POST_CHECKPOINT_DRAIN_SECONDS", 0.01),
        ):
            reason = await capture.capture_segment(
                root,
                segment,
                manifest,
                manifest_path,
                {"next_capture_ordinal": 1},
                stop_requested,
            )

        self.assertEqual(reason, "graceful_checkpoint")
        self.assertEqual(segment["frames"], 4)
        self.assertEqual(segment["order_events"], 1)
        self.assertEqual(segment["trade_events"], 1)
        self.assertEqual(segment["snapshot_metadata"]["microtimestamp"], "150")
        self.assertEqual(segment["checkpoint_metadata"]["microtimestamp"], "200")
        self.assertEqual(
            segment["checkpoint_metadata"]["capture_ordinal_after_drain"], 4
        )
        self.assertTrue((root / "checkpoint-0000.snapshot").exists())

    def _seed_payloads(self):
        return [
            {
                "event": "bts:subscription_succeeded",
                "channel": capture.ORDER_CHANNEL,
                "data": {},
            },
            {
                "event": "bts:subscription_succeeded",
                "channel": capture.TRADE_CHANNEL,
                "data": {},
            },
            {
                "event": "order_created",
                "channel": capture.ORDER_CHANNEL,
                "event_id": ORDER_EVENT_ID,
                "pre_event_id": PRE_ORDER_EVENT_ID,
                "data": {"microtimestamp": "101"},
            },
        ]

    async def _run_with_snapshots(self, root, seed_microtimestamps):
        """Drive one segment, serving the given seed microtimestamps in order."""
        websocket = FakeWebSocket(
            json.dumps(payload, separators=(",", ":"))
            for payload in self._seed_payloads()
        )
        served = []

        def fake_fetch_snapshot(path):
            if path.name.startswith("checkpoint-"):
                microtimestamp = "900"
            else:
                microtimestamp = seed_microtimestamps[
                    min(len(served), len(seed_microtimestamps) - 1)
                ]
                served.append(microtimestamp)
            body = json.dumps(
                {"microtimestamp": microtimestamp, "bids": [], "asks": []}
            ).encode("utf-8")
            path.write_bytes(body)
            return {
                "bytes": len(body),
                "sha256": hashlib.sha256(body).hexdigest(),
                "microtimestamp": microtimestamp,
                "requested_at": "2026-01-01T00:00:00.000000Z",
                "received_at": "2026-01-01T00:00:00.100000Z",
            }

        segment = {
            "index": 0,
            "payload": "segment-0000.jsonl",
            "frame_index": "segment-0000.frames.jsonl",
            "snapshot": "segment-0000.snapshot",
            "checkpoint": "checkpoint-0000.snapshot",
        }
        manifest = {
            "runId": "overlap-test-run",
            "lastCaptureOrdinal": 0,
            "segments": [segment],
        }
        manifest_path = root / "manifest.json"
        capture.write_manifest(manifest_path, manifest)
        stop_requested = asyncio.Event()
        stop_requested.set()

        with (
            patch.object(capture, "connect", return_value=FakeConnection(websocket)),
            patch.object(capture, "fetch_snapshot", side_effect=fake_fetch_snapshot),
            patch.object(capture, "POST_CHECKPOINT_DRAIN_SECONDS", 0.01),
            patch.object(capture, "SNAPSHOT_RETRY_DELAY_SECONDS", 0.0),
        ):
            reason = await capture.capture_segment(
                root,
                segment,
                manifest,
                manifest_path,
                {"next_capture_ordinal": 1},
                stop_requested,
            )
        return reason, segment, served

    async def test_stale_snapshot_is_refetched_until_it_overlaps_the_stream(self):
        """A seed older than the first captured event leaves an uncovered window.

        Bitstamp can serve a snapshot whose microtimestamp predates the moment it is served, so
        subscribing first does not by itself guarantee coverage. Observed in the wild after a
        chain-gap reseed: a 378ms uncovered window produced seven unapplicable removes on replay.
        """
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)

        # First two seeds predate the order event at 101; the third does not.
        reason, segment, served = await self._run_with_snapshots(
            root, ["050", "099", "150"]
        )

        self.assertEqual(reason, "graceful_checkpoint")
        self.assertEqual(served, ["050", "099", "150"])
        self.assertEqual(segment["snapshot_metadata"]["microtimestamp"], "150")
        self.assertEqual(segment["snapshot_metadata"]["overlap_attempts"], 3)

    async def test_permanently_stale_snapshot_ends_the_segment_without_a_seed(self):
        """Better to end the segment than to seed a replay from a snapshot with a hole in it."""
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)

        reason, segment, served = await self._run_with_snapshots(root, ["050"])

        self.assertEqual(reason, "snapshot_never_overlapped_stream")
        self.assertEqual(len(served), capture.SNAPSHOT_OVERLAP_ATTEMPTS)
        # No usable seed, so the manifest must not advertise one.
        self.assertNotIn("snapshot_metadata", segment)
        self.assertNotIn("snapshot", segment)
        self.assertNotIn("checkpoint", segment)


if __name__ == "__main__":
    unittest.main()
