"""Capture public Bitstamp trade executions (live_trades) to a JSONL file.

Companion to dump_raw_ws_bitstamp.py. Trades have no venue-provided snapshot endpoint --
there is no "current state" to reconcile against, only a stream of things that already
happened -- so this intentionally skips the segment/snapshot/reconnect machinery that script
needs. Run it alongside dump_raw_ws_bitstamp.py against the same instrument to get a joined
order/trade view of one live session: a resting order that disappears from live_orders with
no order_deleted, matched against a trade appearing here at the same price and time, is the
evidence task #6 (ADR 0011's order_subtype question) needs.

Usage:
    python scripts/dump_raw_ws_bitstamp_trades.py       # run until Ctrl-C
    python scripts/dump_raw_ws_bitstamp_trades.py 600   # run for 10 minutes
"""

import asyncio
import contextlib
import json
import signal
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

from websockets.asyncio.client import connect


URI = "wss://ws.bitstamp.net"
PAIR = "btcusd"
CHANNEL = f"live_trades_{PAIR}"
CAPTURE_PARENT = Path("data/raw")

FLUSH_EVERY = 100
MAX_FRAME_BYTES = 64 * 1024 * 1024


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds").replace(
        "+00:00", "Z"
    )


def make_capture_directory() -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    candidate = CAPTURE_PARENT / f"bitstamp-{PAIR}-trades-{stamp}"
    suffix = 1
    while candidate.exists():
        candidate = CAPTURE_PARENT / f"bitstamp-{PAIR}-trades-{stamp}-{suffix:02d}"
        suffix += 1
    candidate.mkdir(parents=True)
    return candidate


def write_manifest(path: Path, manifest: dict[str, Any]) -> None:
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def build_subscription() -> str:
    return json.dumps({"event": "bts:subscribe", "data": {"channel": CHANNEL}})


async def run_capture(run_directory: Path, duration: Optional[float]) -> None:
    manifest_path = run_directory / "manifest.json"
    payload_path = run_directory / "trades.jsonl"
    manifest: dict[str, Any] = {
        "format_version": 1,
        "venue": "bitstamp",
        "instrument": PAIR,
        "channel": CHANNEL,
        "websocket_uri": URI,
        "created_at": utc_now(),
        "status": "running",
        "payload": "trades.jsonl",
        "messages": 0,
        "trade_events": 0,
        "first_microtimestamp": None,
        "last_microtimestamp": None,
    }
    write_manifest(manifest_path, manifest)
    print(f"capture directory: {run_directory}", flush=True)

    count = 0
    trade_events = 0
    with payload_path.open("x", encoding="utf-8") as sink:
        async with connect(URI, max_size=MAX_FRAME_BYTES) as websocket:
            await websocket.send(build_subscription())

            async def drain() -> None:
                nonlocal count, trade_events
                async for message in websocket:
                    sink.write(message)
                    sink.write("\n")
                    count += 1
                    if count % FLUSH_EVERY == 0:
                        sink.flush()
                        print(f"trades: {count:,} messages", flush=True)

                    try:
                        envelope = json.loads(message)
                    except json.JSONDecodeError:
                        continue
                    if envelope.get("event") != "trade":
                        continue
                    trade_events += 1
                    ts = envelope.get("data", {}).get("microtimestamp")
                    manifest["first_microtimestamp"] = manifest["first_microtimestamp"] or ts
                    manifest["last_microtimestamp"] = ts

            reader = asyncio.create_task(drain())
            try:
                if duration is not None:
                    await asyncio.wait_for(reader, timeout=duration)
                else:
                    await reader
            except (asyncio.TimeoutError, asyncio.CancelledError):
                reader.cancel()
                await asyncio.gather(reader, return_exceptions=True)
            finally:
                sink.flush()

    manifest.update(
        {
            "status": "completed",
            "ended_at": utc_now(),
            "messages": count,
            "trade_events": trade_events,
            "payload_bytes": payload_path.stat().st_size if payload_path.exists() else 0,
        }
    )
    write_manifest(manifest_path, manifest)
    print(f"stopped after {count:,} messages ({trade_events:,} trades)", flush=True)


async def main() -> None:
    if len(sys.argv) > 2:
        raise SystemExit("usage: dump_raw_ws_bitstamp_trades.py [duration_seconds]")
    duration = float(sys.argv[1]) if len(sys.argv) == 2 else None

    run_directory = make_capture_directory()
    task = asyncio.create_task(run_capture(run_directory, duration))
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        with contextlib.suppress(NotImplementedError):
            loop.add_signal_handler(sig, task.cancel)
    with contextlib.suppress(asyncio.CancelledError):
        await task


if __name__ == "__main__":
    asyncio.run(main())
