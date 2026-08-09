"""Slice 1, step 1. Connect to the venue websocket, append raw JSON lines to a file.

This is the first thing you write on the whole project. It is throwaway Python and it
exists so that steps 2 and 3 (the C++ decoder and the binary writer) can be developed
against real data with no networking in the loop.

Run it for an hour before you write any C++.

Usage:
    python scripts/dump_raw_ws.py            # run until Ctrl-C
    python scripts/dump_raw_ws.py 30         # run for 30 seconds, then stop
"""

import asyncio
import base64
import hashlib
import hmac
import json
import os
import sys
import time
from pathlib import Path

from websockets.asyncio.client import connect


URI = "wss://ws-feed.exchange.coinbase.com"
PRODUCT_ID = "BTC-USD"
OUTPUT_PATH = Path("data/raw/btc-usd-level2-batch.jsonl")

FLUSH_EVERY = 1000

# The level2/level3 snapshot is a single frame carrying the whole book, which blows past
# the websockets default max_size of 1 MiB and gets the connection closed with 1009.
# Bounded rather than None so a misbehaving peer cannot exhaust memory.
MAX_FRAME_BYTES = 64 * 1024 * 1024

# The full/level2/level3 channels require authentication. Credentials come from the
# environment, never from this file -- scripts/ is committed.
KEY = os.environ.get("COINBASE_API_KEY")
SECRET = os.environ.get("COINBASE_API_SECRET")
PASSPHRASE = os.environ.get("COINBASE_API_PASSPHRASE")

# Coinbase signs a dummy GET against this path to prove key ownership over the socket.
SIGNATURE_PATH = "/users/self/verify"


def sign(timestamp: str) -> str:
    """HMAC-SHA256 over '{timestamp}GET/users/self/verify', keyed by the decoded secret.

    The secret arrives base64-encoded and must be decoded to raw bytes before use as the
    HMAC key; the resulting digest is then base64-encoded again. Skipping either step
    produces a well-formed signature that the venue rejects.
    """
    message = f"{timestamp}GET{SIGNATURE_PATH}".encode("utf-8")
    hmac_key = base64.b64decode(SECRET)
    digest = hmac.new(hmac_key, message, hashlib.sha256).digest()
    return base64.b64encode(digest).decode("utf-8")


def build_subscription() -> str:
    """The exact frame Coinbase expects. Channel names are lowercase and case-sensitive."""
    subscription = {
        "type": "subscribe",
        "product_ids": [PRODUCT_ID],
        "channels": ["full", "heartbeat"],
    }

    if KEY and SECRET and PASSPHRASE:
        timestamp = str(time.time())
        subscription["signature"] = sign(timestamp)
        subscription["key"] = KEY
        subscription["passphrase"] = PASSPHRASE
        subscription["timestamp"] = timestamp

    return json.dumps(subscription)


async def capture(output_path: Path) -> None:
    """Append every frame the venue sends, verbatim, one JSON object per line.

    Nothing is parsed here. The bytes on disk must be byte-identical to what the venue
    sent, because the C++ decoder in step 8 is tested against this file.
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with output_path.open("a", encoding="utf-8") as sink:
        async with connect(URI, max_size=MAX_FRAME_BYTES) as websocket:
            await websocket.send(build_subscription())

            message_count = 0
            try:
                async for message in websocket:
                    sink.write(message)
                    sink.write("\n")
                    message_count += 1

                    if message_count % FLUSH_EVERY == 0:
                        sink.flush()
                        print(f"{message_count} messages", flush=True)
            finally:
                sink.flush()
                print(f"stopped after {message_count} messages", flush=True)


async def main() -> None:
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else None

    if duration is None:
        await capture(OUTPUT_PATH)
        return

    try:
        await asyncio.wait_for(capture(OUTPUT_PATH), timeout=duration)
    except asyncio.TimeoutError:
        pass


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
