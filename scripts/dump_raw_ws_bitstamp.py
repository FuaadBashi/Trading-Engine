"""Capture joined Bitstamp L3 orders and trades as snapshot-backed segments.

Each run owns one WebSocket connection and subscribes to both ``live_orders`` and
``live_trades``. Every inbound text frame is preserved unchanged in the segment's
payload-only JSONL file. A second JSONL index records its shared arrival ordinal,
stream kind, timestamps, run/segment identity, and payload line number.

The raw payload file is authoritative: derived metadata never rewrites it. The
``captureOrdinal`` is local observation order, not a claim about Bitstamp's matching
engine order. A reconnect, order-chain gap, or transport failure closes a segment;
the next segment starts only after a new snapshot.

Usage:
    python scripts/dump_raw_ws_bitstamp.py       # run until Ctrl-C
    python scripts/dump_raw_ws_bitstamp.py 3600  # run for one hour
"""

import asyncio
import contextlib
import hashlib
import json
import signal
import sys
import time
import urllib.request
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

from websockets.asyncio.client import connect


URI = "wss://ws.bitstamp.net"
PAIR = "btcusd"
ORDER_CHANNEL = f"live_orders_{PAIR}"
TRADE_CHANNEL = f"live_trades_{PAIR}"
CHANNELS = (ORDER_CHANNEL, TRADE_CHANNEL)
SNAPSHOT_URL = f"https://www.bitstamp.net/api/v2/order_book/{PAIR}/?group=2"
CAPTURE_PARENT = Path("data/raw")

FLUSH_EVERY = 1000
MAX_FRAME_BYTES = 64 * 1024 * 1024
RECONNECT_DELAY_SECONDS = 1.0
SUBSCRIPTION_TIMEOUT_SECONDS = 30.0
POST_CHECKPOINT_DRAIN_SECONDS = 5.0

# A snapshot is only usable as a replay seed if the stream already covers the instant it
# describes. Bitstamp's REST snapshot can carry a microtimestamp several hundred milliseconds
# older than the moment it is served, so subscribing first is necessary but not sufficient --
# the overlap has to be checked and the snapshot refetched when it is stale.
SNAPSHOT_OVERLAP_ATTEMPTS = 5
SNAPSHOT_RETRY_DELAY_SECONDS = 1.0
ORDER_EVENTS = {"order_created", "order_changed", "order_deleted"}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds").replace(
        "+00:00", "Z"
    )


def make_capture_directory() -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    candidate = CAPTURE_PARENT / f"bitstamp-{PAIR}-{stamp}"
    suffix = 1
    while candidate.exists():
        candidate = CAPTURE_PARENT / f"bitstamp-{PAIR}-{stamp}-{suffix:02d}"
        suffix += 1
    candidate.mkdir(parents=True)
    return candidate


def write_manifest(path: Path, manifest: dict[str, Any]) -> None:
    """Atomically replace the manifest so interruption cannot leave partial JSON."""
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def build_subscription(channel: str) -> str:
    return json.dumps({"event": "bts:subscribe", "data": {"channel": channel}})


def fetch_snapshot(path: Path) -> dict[str, Any]:
    """Store one REST snapshot verbatim and return metadata used by the manifest."""
    requested_at = utc_now()
    with urllib.request.urlopen(SNAPSHOT_URL, timeout=30) as response:
        body = response.read()
    received_at = utc_now()

    snapshot = json.loads(body)
    microtimestamp = snapshot.get("microtimestamp")
    if microtimestamp is None:
        raise ValueError("snapshot is missing microtimestamp")

    path.write_bytes(body)
    print(f"snapshot: {len(body):,} bytes -> {path}", flush=True)
    return {
        "bytes": len(body),
        "sha256": hashlib.sha256(body).hexdigest(),
        "microtimestamp": str(microtimestamp),
        "requested_at": requested_at,
        "received_at": received_at,
    }


def classify_message(envelope: dict[str, Any]) -> tuple[str, Optional[str]]:
    """Return the immutable frame's stream kind and venue timestamp, if present."""
    event = envelope.get("event")
    channel = envelope.get("channel")
    data = envelope.get("data")
    microtimestamp = None
    if isinstance(data, dict) and data.get("microtimestamp") is not None:
        microtimestamp = str(data["microtimestamp"])

    if event in ORDER_EVENTS and channel == ORDER_CHANNEL:
        return "order", microtimestamp
    if event == "trade" and channel == TRADE_CHANNEL:
        return "trade", microtimestamp
    return "control", microtimestamp


def make_frame(
    capture_ordinal: int,
    run_id: str,
    segment_id: int,
    payload_line: int,
    stream_kind: str,
    venue_timestamp_micros: Optional[str],
) -> dict[str, Any]:
    """Build metadata that points at, rather than rewrites, the raw payload line."""
    return {
        "captureOrdinal": capture_ordinal,
        "localWallTimestampNanos": time.time_ns(),
        "localSteadyTimestampNanos": time.monotonic_ns(),
        "streamKind": stream_kind,
        "venueTimestampMicros": venue_timestamp_micros,
        "runId": run_id,
        "segmentId": segment_id,
        "payloadLine": payload_line,
    }


def write_json_line(sink: Any, value: dict[str, Any], digest: Any) -> None:
    line = json.dumps(value, separators=(",", ":"), ensure_ascii=False) + "\n"
    sink.write(line)
    digest.update(line.encode("utf-8"))


async def capture_segment(
    run_directory: Path,
    segment: dict[str, Any],
    manifest: dict[str, Any],
    manifest_path: Path,
    capture_state: dict[str, int],
    stop_requested: asyncio.Event,
) -> str:
    """Capture one continuity segment and return the reason it ended."""
    index = segment["index"]
    payload_path = run_directory / segment["payload"]
    frames_path = run_directory / segment["frame_index"]
    snapshot_path = run_directory / segment["snapshot"]
    checkpoint_path = run_directory / segment["checkpoint"]
    run_id = manifest["runId"]

    state: dict[str, Any] = {
        "frames": 0,
        "order_events": 0,
        "trade_events": 0,
        "control_frames": 0,
        "previous_event_id": None,
        "first_event_id": None,
        "last_event_id": None,
        "first_order_microtimestamp": None,
        "last_order_microtimestamp": None,
        "first_trade_microtimestamp": None,
        "last_trade_microtimestamp": None,
        "first_capture_ordinal": None,
        "last_capture_ordinal": None,
        "subscriptions": set(),
        "end_reason": "connection_closed",
        "diagnostic": None,
    }
    payload_digest = hashlib.sha256()
    frames_digest = hashlib.sha256()
    cancellation: Optional[asyncio.CancelledError] = None

    try:
        with (
            payload_path.open("x", encoding="utf-8", newline="\n") as payload_sink,
            frames_path.open("x", encoding="utf-8", newline="\n") as frames_sink,
        ):
            async with connect(URI, max_size=MAX_FRAME_BYTES) as websocket:
                for channel in CHANNELS:
                    await websocket.send(build_subscription(channel))

                subscriptions_ready = asyncio.Event()
                first_order_seen = asyncio.Event()

                async def drain() -> None:
                    async for message in websocket:
                        state["frames"] += 1
                        payload_line = state["frames"]
                        capture_ordinal = capture_state["next_capture_ordinal"]
                        capture_state["next_capture_ordinal"] += 1
                        state["first_capture_ordinal"] = (
                            state["first_capture_ordinal"] or capture_ordinal
                        )
                        state["last_capture_ordinal"] = capture_ordinal

                        raw_line = message + "\n"
                        payload_sink.write(raw_line)
                        payload_digest.update(raw_line.encode("utf-8"))

                        try:
                            envelope = json.loads(message)
                        except json.JSONDecodeError as exc:
                            frame = make_frame(
                                capture_ordinal,
                                run_id,
                                index,
                                payload_line,
                                "control",
                                None,
                            )
                            write_json_line(frames_sink, frame, frames_digest)
                            state["control_frames"] += 1
                            state["end_reason"] = "malformed_payload"
                            state["diagnostic"] = {
                                "payload_line": payload_line,
                                "error": str(exc),
                            }
                            return

                        if not isinstance(envelope, dict):
                            frame = make_frame(
                                capture_ordinal,
                                run_id,
                                index,
                                payload_line,
                                "control",
                                None,
                            )
                            write_json_line(frames_sink, frame, frames_digest)
                            state["control_frames"] += 1
                            state["end_reason"] = "malformed_payload"
                            state["diagnostic"] = {
                                "payload_line": payload_line,
                                "error": "payload is not a JSON object",
                            }
                            return

                        stream_kind, microtimestamp = classify_message(envelope)
                        frame = make_frame(
                            capture_ordinal,
                            run_id,
                            index,
                            payload_line,
                            stream_kind,
                            microtimestamp,
                        )
                        write_json_line(frames_sink, frame, frames_digest)

                        if stream_kind == "order":
                            state["order_events"] += 1
                            state["first_order_microtimestamp"] = (
                                state["first_order_microtimestamp"] or microtimestamp
                            )
                            first_order_seen.set()
                            state["last_order_microtimestamp"] = microtimestamp
                        elif stream_kind == "trade":
                            state["trade_events"] += 1
                            state["first_trade_microtimestamp"] = (
                                state["first_trade_microtimestamp"] or microtimestamp
                            )
                            state["last_trade_microtimestamp"] = microtimestamp
                        else:
                            state["control_frames"] += 1

                        if state["frames"] % FLUSH_EVERY == 0:
                            payload_sink.flush()
                            frames_sink.flush()
                            print(
                                f"segment {index:04d}: {state['frames']:,} frames "
                                f"({state['order_events']:,} orders, "
                                f"{state['trade_events']:,} trades)",
                                flush=True,
                            )

                        event = envelope.get("event")
                        channel = envelope.get("channel")
                        if event == "bts:subscription_succeeded":
                            if channel not in CHANNELS:
                                state["end_reason"] = "unexpected_subscription"
                                state["diagnostic"] = {"channel": channel}
                                return
                            if channel in state["subscriptions"]:
                                state["end_reason"] = "duplicate_subscription"
                                state["diagnostic"] = {"channel": channel}
                                return
                            state["subscriptions"].add(channel)
                            if state["subscriptions"] == set(CHANNELS):
                                subscriptions_ready.set()
                            continue

                        if event == "bts:request_reconnect":
                            state["end_reason"] = "venue_requested_reconnect"
                            return
                        if event == "bts:error":
                            state["end_reason"] = "venue_error"
                            state["diagnostic"] = envelope.get("data")
                            return
                        if event == "trade" and channel != TRADE_CHANNEL:
                            state["end_reason"] = "unexpected_trade_channel"
                            state["diagnostic"] = {"channel": channel}
                            return
                        if event not in ORDER_EVENTS:
                            continue
                        if channel != ORDER_CHANNEL:
                            state["end_reason"] = "unexpected_order_channel"
                            state["diagnostic"] = {"channel": channel}
                            return

                        event_id = envelope.get("event_id")
                        pre_event_id = envelope.get("pre_event_id")
                        if event_id is None or pre_event_id is None:
                            state["end_reason"] = "malformed_order_event"
                            state["diagnostic"] = {
                                "payload_line": payload_line,
                                "missing": "event_id or pre_event_id",
                            }
                            return

                        previous = state["previous_event_id"]
                        if previous is not None and pre_event_id != previous:
                            state["end_reason"] = "chain_gap"
                            state["diagnostic"] = {
                                "payload_line": payload_line,
                                "expected_pre_event_id": previous,
                                "actual_pre_event_id": pre_event_id,
                                "microtimestamp": microtimestamp,
                            }
                            return

                        state["previous_event_id"] = event_id
                        state["first_event_id"] = state["first_event_id"] or event_id
                        state["last_event_id"] = event_id

                # Begin reading before the blocking REST request. Waiting for both
                # acknowledgements proves the single connection is carrying both sources;
                # frames received before the snapshot remain in the raw prefix for cutoff logic.
                reader = asyncio.create_task(drain())
                subscription_waiter = asyncio.create_task(subscriptions_ready.wait())
                stop_waiter: Optional[asyncio.Task[bool]] = None
                try:
                    done, _ = await asyncio.wait(
                        {reader, subscription_waiter},
                        timeout=SUBSCRIPTION_TIMEOUT_SECONDS,
                        return_when=asyncio.FIRST_COMPLETED,
                    )
                    subscribed = subscription_waiter in done and not reader.done()
                    seeded = subscribed

                    if seeded:
                        # Subscribing first is not enough on its own: the served snapshot can
                        # describe the book as it was before this stream started, leaving a window
                        # no source covers. Wait for one order event so there is something to
                        # compare against.
                        first_order_waiter = asyncio.create_task(first_order_seen.wait())
                        await asyncio.wait(
                            {reader, first_order_waiter},
                            timeout=SUBSCRIPTION_TIMEOUT_SECONDS,
                            return_when=asyncio.FIRST_COMPLETED,
                        )
                        if not first_order_waiter.done():
                            first_order_waiter.cancel()
                            state["end_reason"] = "no_order_event_before_snapshot"
                            seeded = False
                        elif reader.done():
                            seeded = False

                    if seeded:
                        # Refetch until the snapshot is at least as new as our first captured
                        # order event. Without this the replay seed predates the stream and every
                        # order created in the uncovered window looks unknown when its delete
                        # arrives. Observed once in the wild: a reseed after a chain gap produced
                        # a snapshot 378ms stale and seven unapplicable removes.
                        snapshot_metadata = None
                        for attempt in range(SNAPSHOT_OVERLAP_ATTEMPTS):
                            try:
                                candidate = await (
                                    asyncio.get_running_loop().run_in_executor(
                                        None, fetch_snapshot, snapshot_path
                                    )
                                )
                            except Exception:
                                state["end_reason"] = "snapshot_error"
                                raise
                            first_order = state["first_order_microtimestamp"]
                            if first_order is None or int(candidate["microtimestamp"]) >= int(
                                first_order
                            ):
                                candidate["overlap_attempts"] = attempt + 1
                                snapshot_metadata = candidate
                                break
                            stale_micros = int(first_order) - int(candidate["microtimestamp"])
                            print(
                                f"snapshot predates stream by {stale_micros / 1e6:.3f}s "
                                f"(attempt {attempt + 1}/{SNAPSHOT_OVERLAP_ATTEMPTS}); refetching",
                                flush=True,
                            )
                            if reader.done():
                                break
                            await asyncio.sleep(SNAPSHOT_RETRY_DELAY_SECONDS)

                        if snapshot_metadata is None:
                            # Every attempt left an uncovered window, so this segment has no valid
                            # seed. Ending here is the honest outcome: a replay from a stale
                            # snapshot silently produces a wrong book.
                            state["end_reason"] = "snapshot_never_overlapped_stream"
                            seeded = False

                    if seeded:
                        segment["snapshot_metadata"] = snapshot_metadata
                        write_manifest(manifest_path, manifest)

                        stop_waiter = asyncio.create_task(stop_requested.wait())
                        done, _ = await asyncio.wait(
                            {reader, stop_waiter},
                            return_when=asyncio.FIRST_COMPLETED,
                        )
                        if reader in done:
                            await reader
                        else:
                            checkpoint_request_ordinal = (
                                capture_state["next_capture_ordinal"] - 1
                            )
                            try:
                                checkpoint_metadata = await (
                                    asyncio.get_running_loop().run_in_executor(
                                        None, fetch_snapshot, checkpoint_path
                                    )
                                )
                            except Exception:
                                state["end_reason"] = "checkpoint_error"
                                raise

                            checkpoint_metadata.update(
                                {
                                    "capture_ordinal_before_request": (
                                        checkpoint_request_ordinal
                                    ),
                                    "capture_ordinal_at_receive": (
                                        capture_state["next_capture_ordinal"] - 1
                                    ),
                                    "post_checkpoint_drain_seconds": (
                                        POST_CHECKPOINT_DRAIN_SECONDS
                                    ),
                                }
                            )
                            segment["checkpoint_metadata"] = checkpoint_metadata
                            write_manifest(manifest_path, manifest)

                            if reader.done():
                                await reader
                            else:
                                done, _ = await asyncio.wait(
                                    {reader}, timeout=POST_CHECKPOINT_DRAIN_SECONDS
                                )
                                if reader in done:
                                    await reader
                                else:
                                    state["end_reason"] = "graceful_checkpoint"
                                    reader.cancel()
                                    await asyncio.gather(
                                        reader, return_exceptions=True
                                    )
                    elif subscribed:
                        # Subscriptions were fine; seeding failed and already recorded a specific
                        # reason. Do not overwrite it with the generic subscription timeout.
                        reader.cancel()
                        await asyncio.gather(reader, return_exceptions=True)
                    elif reader in done:
                        await reader
                    else:
                        state["end_reason"] = "subscription_timeout"
                        reader.cancel()
                        await asyncio.gather(reader, return_exceptions=True)
                except asyncio.CancelledError as exc:
                    state["end_reason"] = "forced_cancel"
                    cancellation = exc
                    reader.cancel()
                    await asyncio.gather(reader, return_exceptions=True)
                except Exception:
                    reader.cancel()
                    await asyncio.gather(reader, return_exceptions=True)
                    raise
                finally:
                    subscription_waiter.cancel()
                    await asyncio.gather(subscription_waiter, return_exceptions=True)
                    if stop_waiter is not None:
                        stop_waiter.cancel()
                        await asyncio.gather(stop_waiter, return_exceptions=True)
                    payload_sink.flush()
                    frames_sink.flush()
    except asyncio.CancelledError as exc:
        state["end_reason"] = "forced_cancel"
        cancellation = exc
    except Exception as exc:
        if state["end_reason"] == "connection_closed":
            state["end_reason"] = "connection_or_capture_error"
            state["diagnostic"] = {
                "exception": type(exc).__name__,
                "message": str(exc),
            }
        raise
    finally:
        manifest["lastCaptureOrdinal"] = capture_state["next_capture_ordinal"] - 1
        segment.update(
            {
                "ended_at": utc_now(),
                "end_reason": state["end_reason"],
                "frames": state["frames"],
                "order_events": state["order_events"],
                "trade_events": state["trade_events"],
                "control_frames": state["control_frames"],
                "payload_bytes": payload_path.stat().st_size
                if payload_path.exists()
                else 0,
                "payload_sha256": payload_digest.hexdigest(),
                "frames_bytes": frames_path.stat().st_size if frames_path.exists() else 0,
                "frames_sha256": frames_digest.hexdigest(),
                "first_event_id": state["first_event_id"],
                "last_event_id": state["last_event_id"],
                "first_order_microtimestamp": state["first_order_microtimestamp"],
                "last_order_microtimestamp": state["last_order_microtimestamp"],
                "first_trade_microtimestamp": state["first_trade_microtimestamp"],
                "last_trade_microtimestamp": state["last_trade_microtimestamp"],
                "first_capture_ordinal": state["first_capture_ordinal"],
                "last_capture_ordinal": state["last_capture_ordinal"],
                "subscriptions": sorted(state["subscriptions"]),
                "chain_valid": state["end_reason"] != "chain_gap",
            }
        )
        if state["diagnostic"] is not None:
            segment["diagnostic"] = state["diagnostic"]

        # Only name files that were actually written. A segment ending on a chain gap never
        # reaches its terminal checkpoint, and a segment whose snapshot never overlapped the
        # stream has no seed; advertising either path makes the manifest disagree with the disk
        # and fails every reader that trusts it.
        if "checkpoint_metadata" not in segment:
            segment.pop("checkpoint", None)
        if "snapshot_metadata" not in segment:
            segment.pop("snapshot", None)

        checkpoint_metadata = segment.get("checkpoint_metadata")
        if isinstance(checkpoint_metadata, dict):
            checkpoint_metadata["capture_ordinal_after_drain"] = (
                capture_state["next_capture_ordinal"] - 1
            )
            checkpoint_metadata["drain_completed_at"] = utc_now()
        write_manifest(manifest_path, manifest)
        print(
            f"segment {index:04d}: stopped after {state['frames']:,} frames "
            f"({state['order_events']:,} orders, {state['trade_events']:,} trades; "
            f"{state['end_reason']})",
            flush=True,
        )

    if cancellation is not None:
        raise cancellation
    return str(state["end_reason"])


async def run_capture(run_directory: Path, stop_requested: asyncio.Event) -> None:
    manifest_path = run_directory / "manifest.json"
    manifest: dict[str, Any] = {
        "format_version": 2,
        "capture_format": "bitstamp_joined_frames_v1",
        "venue": "bitstamp",
        "instrument": PAIR,
        "channels": list(CHANNELS),
        "websocket_uri": URI,
        "snapshot_url": SNAPSHOT_URL,
        "runId": str(uuid.uuid4()),
        "created_at": utc_now(),
        "status": "running",
        "lastCaptureOrdinal": 0,
        "segments": [],
    }
    capture_state = {"next_capture_ordinal": 1}
    write_manifest(manifest_path, manifest)
    print(f"capture directory: {run_directory}", flush=True)

    try:
        while True:
            index = len(manifest["segments"])
            segment = {
                "index": index,
                "payload": f"segment-{index:04d}.jsonl",
                "frame_index": f"segment-{index:04d}.frames.jsonl",
                "snapshot": f"segment-{index:04d}.snapshot",
                "checkpoint": f"checkpoint-{index:04d}.snapshot",
                "started_at": utc_now(),
                "start_reason": "capture_started" if index == 0 else "reconnected",
            }
            manifest["segments"].append(segment)
            write_manifest(manifest_path, manifest)

            reason = await capture_segment(
                run_directory,
                segment,
                manifest,
                manifest_path,
                capture_state,
                stop_requested,
            )
            if reason == "graceful_checkpoint":
                manifest["status"] = "completed"
                manifest["ended_at"] = utc_now()
                write_manifest(manifest_path, manifest)
                return
            if reason in {
                "venue_requested_reconnect",
                "chain_gap",
                "connection_closed",
            }:
                if stop_requested.is_set():
                    raise RuntimeError(
                        "capture stopped without a healthy terminal checkpoint"
                    )
                try:
                    await asyncio.wait_for(
                        stop_requested.wait(), timeout=RECONNECT_DELAY_SECONDS
                    )
                except asyncio.TimeoutError:
                    continue
                raise RuntimeError(
                    "capture stopped during reconnect without a terminal checkpoint"
                )
            raise RuntimeError(f"segment ended because of {reason}")
    except asyncio.CancelledError:
        manifest["status"] = "interrupted"
        manifest["ended_at"] = utc_now()
        manifest["error"] = "capture was force-cancelled before checkpoint completion"
        write_manifest(manifest_path, manifest)
        raise
    except Exception as exc:
        manifest["status"] = "failed"
        manifest["ended_at"] = utc_now()
        manifest["error"] = str(exc)
        write_manifest(manifest_path, manifest)
        raise


async def main() -> None:
    if len(sys.argv) > 2:
        raise SystemExit("usage: dump_raw_ws_bitstamp.py [duration_seconds]")
    duration = float(sys.argv[1]) if len(sys.argv) == 2 else None
    if duration is not None and duration <= 0:
        raise SystemExit("duration_seconds must be positive")

    run_directory = make_capture_directory()
    stop_requested = asyncio.Event()
    task = asyncio.create_task(run_capture(run_directory, stop_requested))
    loop = asyncio.get_running_loop()

    def request_stop() -> None:
        if stop_requested.is_set():
            print("forcing capture cancellation", flush=True)
            task.cancel()
            return
        print(
            "graceful stop requested; fetching terminal checkpoint",
            flush=True,
        )
        stop_requested.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        with contextlib.suppress(NotImplementedError):
            loop.add_signal_handler(sig, request_stop)

    timer = None
    if duration is not None:
        timer = loop.call_later(duration, request_stop)

    try:
        await task
    except asyncio.CancelledError:
        pass
    finally:
        if timer is not None:
            timer.cancel()
        for sig in (signal.SIGINT, signal.SIGTERM):
            with contextlib.suppress(NotImplementedError):
                loop.remove_signal_handler(sig)


if __name__ == "__main__":
    asyncio.run(main())
