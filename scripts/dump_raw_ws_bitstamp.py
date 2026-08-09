"""Capture public Bitstamp L3 events as snapshot-backed segments.

Each run creates a directory containing a manifest and one or more segments. Every
segment has its own group=2 REST snapshot and payload-only JSONL stream. A requested
reconnect, chain gap, or transport failure closes the segment; a new connection and
snapshot are required before capture resumes. This implements ADR 0006.

The JSONL stores each decoded WebSocket text payload unchanged, followed by a newline.
WebSocket framing and compression are handled by the websockets library, so the file is
payload-preserving rather than byte-identical to the network wire.

Usage:
    python scripts/dump_raw_ws_bitstamp.py       # run until Ctrl-C
    python scripts/dump_raw_ws_bitstamp.py 3600  # run for one hour
"""

import asyncio
import contextlib
import json
import signal
import sys
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

from websockets.asyncio.client import connect


URI = "wss://ws.bitstamp.net"
PAIR = "btcusd"
CHANNEL = f"live_orders_{PAIR}"
SNAPSHOT_URL = f"https://www.bitstamp.net/api/v2/order_book/{PAIR}/?group=2"
CAPTURE_PARENT = Path("data/raw")

FLUSH_EVERY = 1000
MAX_FRAME_BYTES = 64 * 1024 * 1024
RECONNECT_DELAY_SECONDS = 1.0
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


def build_subscription() -> str:
    return json.dumps({"event": "bts:subscribe", "data": {"channel": CHANNEL}})


def fetch_snapshot(path: Path) -> dict[str, Any]:
    """Store one REST snapshot verbatim and return metadata used by the manifest."""
    with urllib.request.urlopen(SNAPSHOT_URL, timeout=30) as response:
        body = response.read()

    snapshot = json.loads(body)
    microtimestamp = snapshot.get("microtimestamp")
    if microtimestamp is None:
        raise ValueError("snapshot is missing microtimestamp")

    path.write_bytes(body)
    print(f"snapshot: {len(body):,} bytes -> {path}", flush=True)
    return {
        "bytes": len(body),
        "microtimestamp": str(microtimestamp),
        "received_at": utc_now(),
    }


async def capture_segment(
    run_directory: Path,
    segment: dict[str, Any],
    manifest: dict[str, Any],
    manifest_path: Path,
) -> str:
    """Capture one continuity segment and return the reason it ended."""
    index = segment["index"]
    payload_path = run_directory / segment["payload"]
    snapshot_path = run_directory / segment["snapshot"]

    state: dict[str, Any] = {
        "count": 0,
        "order_events": 0,
        "previous_event_id": None,
        "first_event_id": None,
        "last_event_id": None,
        "first_microtimestamp": None,
        "last_microtimestamp": None,
        "end_reason": "connection_closed",
        "diagnostic": None,
    }

    cancellation: Optional[asyncio.CancelledError] = None

    try:
        with payload_path.open("x", encoding="utf-8") as sink:
            async with connect(URI, max_size=MAX_FRAME_BYTES) as websocket:
                await websocket.send(build_subscription())

                async def drain() -> None:
                    async for message in websocket:
                        sink.write(message)
                        sink.write("\n")
                        state["count"] += 1

                        if state["count"] % FLUSH_EVERY == 0:
                            sink.flush()
                            print(
                                f"segment {index:04d}: {state['count']:,} messages",
                                flush=True,
                            )

                        try:
                            envelope = json.loads(message)
                        except json.JSONDecodeError as exc:
                            state["end_reason"] = "malformed_payload"
                            state["diagnostic"] = {"error": str(exc)}
                            return

                        event = envelope.get("event")
                        if event == "bts:request_reconnect":
                            state["end_reason"] = "venue_requested_reconnect"
                            return
                        if event == "bts:error":
                            state["end_reason"] = "venue_error"
                            state["diagnostic"] = envelope.get("data")
                            return
                        if event not in ORDER_EVENTS:
                            continue

                        event_id = envelope.get("event_id")
                        pre_event_id = envelope.get("pre_event_id")
                        microtimestamp = envelope.get("data", {}).get("microtimestamp")
                        if event_id is None or pre_event_id is None:
                            state["end_reason"] = "malformed_order_event"
                            state["diagnostic"] = {
                                "message_number": state["count"],
                                "missing": "event_id or pre_event_id",
                            }
                            return

                        previous = state["previous_event_id"]
                        if previous is not None and pre_event_id != previous:
                            state["end_reason"] = "chain_gap"
                            state["diagnostic"] = {
                                "message_number": state["count"],
                                "expected_pre_event_id": previous,
                                "actual_pre_event_id": pre_event_id,
                                "microtimestamp": microtimestamp,
                            }
                            return

                        state["order_events"] += 1
                        state["previous_event_id"] = event_id
                        state["first_event_id"] = state["first_event_id"] or event_id
                        state["last_event_id"] = event_id
                        state["first_microtimestamp"] = (
                            state["first_microtimestamp"] or microtimestamp
                        )
                        state["last_microtimestamp"] = microtimestamp

                # Draining must begin before the blocking HTTP snapshot request. Events
                # received during that request are buffered in this segment and replay
                # can discard those at or before the snapshot microtimestamp.
                reader = asyncio.create_task(drain())
                try:
                    snapshot_metadata = await asyncio.get_running_loop().run_in_executor(
                        None, fetch_snapshot, snapshot_path
                    )
                    segment["snapshot_metadata"] = snapshot_metadata
                    write_manifest(manifest_path, manifest)
                    await reader
                except asyncio.CancelledError as exc:
                    state["end_reason"] = "capture_stopped"
                    cancellation = exc
                    reader.cancel()
                    await asyncio.gather(reader, return_exceptions=True)
                except Exception:
                    state["end_reason"] = "snapshot_error"
                    reader.cancel()
                    await asyncio.gather(reader, return_exceptions=True)
                    raise
                finally:
                    sink.flush()
    except asyncio.CancelledError as exc:
        state["end_reason"] = "capture_stopped"
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
        segment.update(
            {
                "ended_at": utc_now(),
                "end_reason": state["end_reason"],
                "messages": state["count"],
                "order_events": state["order_events"],
                "payload_bytes": payload_path.stat().st_size
                if payload_path.exists()
                else 0,
                "first_event_id": state["first_event_id"],
                "last_event_id": state["last_event_id"],
                "first_microtimestamp": state["first_microtimestamp"],
                "last_microtimestamp": state["last_microtimestamp"],
                "chain_valid": state["end_reason"] != "chain_gap",
            }
        )
        if state["diagnostic"] is not None:
            segment["diagnostic"] = state["diagnostic"]
        write_manifest(manifest_path, manifest)
        print(
            f"segment {index:04d}: stopped after {state['count']:,} messages "
            f"({state['end_reason']})",
            flush=True,
        )

    if cancellation is not None:
        raise cancellation
    return str(state["end_reason"])


async def run_capture(run_directory: Path) -> None:
    manifest_path = run_directory / "manifest.json"
    manifest: dict[str, Any] = {
        "format_version": 1,
        "venue": "bitstamp",
        "instrument": PAIR,
        "channel": CHANNEL,
        "websocket_uri": URI,
        "snapshot_url": SNAPSHOT_URL,
        "created_at": utc_now(),
        "status": "running",
        "segments": [],
    }
    write_manifest(manifest_path, manifest)
    print(f"capture directory: {run_directory}", flush=True)

    try:
        while True:
            index = len(manifest["segments"])
            segment = {
                "index": index,
                "payload": f"segment-{index:04d}.jsonl",
                "snapshot": f"segment-{index:04d}.snapshot",
                "started_at": utc_now(),
                "start_reason": "capture_started" if index == 0 else "reconnected",
            }
            manifest["segments"].append(segment)
            write_manifest(manifest_path, manifest)

            reason = await capture_segment(
                run_directory, segment, manifest, manifest_path
            )
            if reason in {
                "venue_requested_reconnect",
                "chain_gap",
                "connection_closed",
            }:
                await asyncio.sleep(RECONNECT_DELAY_SECONDS)
                continue
            if reason in {"venue_error", "malformed_payload", "malformed_order_event"}:
                raise RuntimeError(f"segment ended because of {reason}")
    except asyncio.CancelledError:
        manifest["status"] = "completed"
        manifest["ended_at"] = utc_now()
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
    task = asyncio.create_task(run_capture(run_directory))
    loop = asyncio.get_running_loop()

    for sig in (signal.SIGINT, signal.SIGTERM):
        with contextlib.suppress(NotImplementedError):
            loop.add_signal_handler(sig, task.cancel)

    timer = None
    if duration is not None:
        timer = loop.call_later(duration, task.cancel)

    try:
        await task
    except asyncio.CancelledError:
        pass
    finally:
        if timer is not None:
            timer.cancel()


if __name__ == "__main__":
    asyncio.run(main())
