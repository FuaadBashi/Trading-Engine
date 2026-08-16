import json
import tempfile
import unittest
from pathlib import Path

from scripts.audit_book_bootstrap import audit_segment


def order_event(
    event: str,
    event_id: str,
    predecessor: str,
    order_id: str,
    side: int,
    price: str,
    quantity: str,
    microtimestamp: str,
) -> dict:
    return {
        "event": event,
        "event_id": event_id,
        "pre_event_id": predecessor,
        "data": {
            "id_str": order_id,
            "order_type": side,
            "price_str": price,
            "amount_str": quantity,
            "microtimestamp": microtimestamp,
        },
    }


class BootstrapAuditTest(unittest.TestCase):
    def write_capture(self, snapshot: dict, events: list[dict]):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        snapshot_path = root / "segment.snapshot"
        payload_path = root / "segment.jsonl"
        snapshot_path.write_text(json.dumps(snapshot), encoding="utf-8")
        payload_path.write_text(
            "".join(json.dumps(event) + "\n" for event in events),
            encoding="utf-8",
        )
        self.addCleanup(temporary.cleanup)
        return snapshot_path, payload_path

    def test_replays_measured_bitstamp_shapes(self):
        snapshot = {
            "microtimestamp": "100",
            "bids": [["100.00", "1.00000000", "1"]],
            "asks": [["101.00", "1.00000000", "2"]],
        }
        events = [
            order_event("order_created", "e1", "before", "3", 1, "102.00", "2.00000000", "110"),
            order_event("order_changed", "e2", "e1", "3", 1, "103.00", "1.00000000", "120"),
            # Delete price is deliberately not the stored resting price.
            order_event("order_deleted", "e3", "e2", "3", 1, "100.50", "0", "130"),
            # Unknown deletion is boundary evidence, not a hard parser failure.
            order_event("order_deleted", "e4", "e3", "999", 0, "99.00", "1.00000000", "140"),
            # Price-zero market lifecycle never becomes a resting level.
            order_event("order_created", "e5", "e4", "4", 0, "0", "1.00000000", "150"),
            order_event("order_deleted", "e6", "e5", "4", 0, "101.00", "0", "160"),
        ]
        snapshot_path, payload_path = self.write_capture(snapshot, events)

        result = audit_segment(snapshot_path, payload_path)

        self.assertEqual(result.errors, [])
        self.assertEqual(result.applied_events, 3)
        self.assertEqual(result.changed_price_moves, 1)
        self.assertEqual(result.deletion_price_differences, 1)
        self.assertEqual(result.zero_price_lifecycle_events, 2)
        self.assertEqual(len(result.unknown_orders), 1)
        self.assertEqual(result.final_best_bid, 10_000)
        self.assertEqual(result.final_best_ask, 10_100)

    def test_chain_break_stops_before_revealing_event(self):
        snapshot = {
            "microtimestamp": "100",
            "bids": [["100.00", "1.00000000", "1"]],
            "asks": [["101.00", "1.00000000", "2"]],
        }
        events = [
            order_event("order_created", "e1", "before", "3", 0, "99.00", "1.00000000", "110"),
            order_event("order_created", "e2", "wrong", "4", 0, "98.00", "1.00000000", "120"),
        ]
        snapshot_path, payload_path = self.write_capture(snapshot, events)

        result = audit_segment(snapshot_path, payload_path)

        self.assertEqual(result.chain_break_line, 2)
        self.assertEqual(result.applied_events, 1)
        self.assertEqual(result.final_orders, 3)
        self.assertEqual(len(result.errors), 1)
        self.assertIn("chain break", result.errors[0])


if __name__ == "__main__":
    unittest.main()
