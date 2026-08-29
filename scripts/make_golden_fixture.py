"""Generate the committed joined-capture golden fixture.

The checkpoint is written by HAND from what the venue would report, never by replaying the book
under test. A fixture whose expected state is derived from the code it checks proves only that the
code agrees with itself.

Scenario, chosen to exercise the paths a real Bitstamp capture does not:
  - events before the seed and after the checkpoint  -> input accounting
  - a fill reported by BOTH streams at one timestamp -> the credit must suppress the correction
  - a fill reported ONLY by live_trades             -> the correction path (never seen in the wild)
  - a genuine cancel (amount_traded == 0)           -> must not be mistaken for a fill
"""

import json
from pathlib import Path
import sys

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)

SEED_TS = 1000000
CUTOFF_TS = 1500000


def order(ts, oid, side, price, amount, traded, event):
    return {
        "data": {
            "id": oid,
            "id_str": str(oid),
            "order_type": side,          # 0 buy, 1 sell
            "microtimestamp": str(ts),
            "amount_str": amount,
            "amount_traded": traded,
            "price_str": price,
        },
        "channel": "live_orders_btcusd",
        "event": event,
    }


def trade(ts, buy_id, sell_id, amount):
    return {
        "data": {
            "microtimestamp": str(ts),
            "amount_str": amount,
            "buy_order_id": buy_id,
            "sell_order_id": sell_id,
        },
        "channel": "live_trades_btcusd",
        "event": "trade",
    }


# --- seed: what the venue reported at SEED_TS -------------------------------------------------
seed = {
    "timestamp": "1",
    "microtimestamp": str(SEED_TS),
    "bids": [
        ["100.00", "2.00000000", "101"],
        ["99.00", "1.00000000", "102"],
    ],
    "asks": [
        ["101.00", "3.00000000", "201"],
        ["102.00", "1.50000000", "202"],
    ],
}

# --- the stream -------------------------------------------------------------------------------
events = [
    # Before the seed: must be counted, never applied.
    ("order", order(900000, 999, 0, "98.00", "1.00000000", "0", "order_created")),

    # A plain add inside the window.
    ("order", order(1100000, 103, 0, "100.00", "0.50000000", "0", "order_created")),

    # Fill reported by BOTH streams at one timestamp. live_orders already carries the post-fill
    # amount, so the credit must absorb the trade and emit no correction. Getting this wrong
    # deletes order 101 outright -- the original ADR 0013 bug.
    ("order", order(1200000, 101, 0, "100.00", "1.50000000", "0.50000000", "order_changed")),
    ("trade", trade(1200000, 101, 888, "0.50000000")),

    # Fill reported ONLY by live_trades: no order event ever arrives for 102. This is the case
    # TradeReconciler exists for and that 1,059s of real capture never produced. The trade fully
    # consumes the order, so the correction must remove it.
    ("trade", trade(1300000, 102, 777, "1.00000000")),

    # A genuine cancel: amount_traded is 0, so it is not a fill and needs no reconciliation.
    ("order", order(1400000, 202, 1, "102.00", "0", "0", "order_deleted")),

    # After the checkpoint: must be counted, never applied.
    ("order", order(1600000, 104, 0, "97.00", "5.00000000", "0", "order_created")),
]

# --- checkpoint: hand-derived from the venue's point of view ----------------------------------
# 101 was 2.00000000, traded 0.50000000 once           -> 1.50000000 resting
# 103 arrived at 0.50000000, untouched                 -> 0.50000000 resting
# 102 was 1.00000000, fully traded at 1300000          -> gone
# 201 never touched                                    -> 3.00000000 resting
# 202 cancelled at 1400000                             -> gone
# 104 arrives after the checkpoint                     -> not present
checkpoint = {
    "timestamp": "2",
    "microtimestamp": str(CUTOFF_TS),
    "bids": [
        ["100.00", "1.50000000", "101"],
        ["100.00", "0.50000000", "103"],
    ],
    "asks": [
        ["101.00", "3.00000000", "201"],
    ],
}

(out / "segment-0000.snapshot").write_text(json.dumps(seed, separators=(",", ":")) + "\n")
(out / "checkpoint-0000.snapshot").write_text(json.dumps(checkpoint, separators=(",", ":")) + "\n")

payload_lines, frame_lines = [], []
for index, (kind, body) in enumerate(events, start=1):
    payload_lines.append(json.dumps(body, separators=(",", ":")))
    frame_lines.append(json.dumps({
        "captureOrdinal": index,
        "streamKind": kind,
        "venueTimestampMicros": int(body["data"]["microtimestamp"]),
        "runId": "golden-fixture",
        "segmentId": 0,
        "payloadLine": index,
    }, separators=(",", ":")))

(out / "segment-0000.jsonl").write_text("\n".join(payload_lines) + "\n")
(out / "segment-0000.frames.jsonl").write_text("\n".join(frame_lines) + "\n")

(out / "manifest.json").write_text(json.dumps({
    "format_version": 2,
    "venue": "bitstamp",
    "instrument": "btcusd",
    "runId": "golden-fixture",
    "segments": [{
        "index": 0,
        "payload": "segment-0000.jsonl",
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot",
        "checkpoint": "checkpoint-0000.snapshot",
    }],
}, indent=2) + "\n")

print(f"wrote fixture to {out}")
for f in sorted(out.iterdir()):
    print(f"  {f.name:32} {f.stat().st_size:>6} bytes")
