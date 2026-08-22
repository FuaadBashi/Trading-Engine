"""Audit snapshot + websocket bootstrapping for the Slice 2 reference book.

This is an offline Python oracle, not the production order book. It loads every order
from a Bitstamp ``group=2`` REST snapshot, applies the paired ``live_orders`` events,
and reports the boundary and consistency evidence that the C++ implementation must
eventually match.

Usage:
    python scripts/audit_book_bootstrap.py data/raw/bitstamp-btcusd-<timestamp>
    python scripts/audit_book_bootstrap.py data/raw/bitstamp-btcusd-<timestamp>/manifest.json
    python scripts/audit_book_bootstrap.py snapshot-file payload-file
"""

import json
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional


ORDER_EVENTS = {"order_created", "order_changed", "order_deleted"}
PRICE_DECIMALS = 2
QUANTITY_DECIMALS = 8


@dataclass(frozen=True)
class RestingOrder:
    order_id: int
    side: str
    price_ticks: int
    quantity_units: int


@dataclass(frozen=True)
class UnknownOrder:
    line: int
    event: str
    order_id: int
    microtimestamp: int


@dataclass
class AuditResult:
    snapshot_path: Path
    payload_path: Path
    snapshot_microtimestamp: int = 0
    snapshot_orders: int = 0
    snapshot_price_levels: int = 0
    payload_lines: int = 0
    order_events: int = 0
    applied_events: int = 0
    events_at_or_before_snapshot: int = 0
    changed_price_moves: int = 0
    deletion_price_differences: int = 0
    zero_price_lifecycle_events: int = 0
    counts: Counter = field(default_factory=Counter)
    unknown_orders: list[UnknownOrder] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    initial_best_bid: Optional[int] = None
    initial_best_ask: Optional[int] = None
    final_best_bid: Optional[int] = None
    final_best_ask: Optional[int] = None
    final_orders: int = 0
    final_price_levels: int = 0
    chain_break_line: Optional[int] = None
    crossed_after_lines: list[int] = field(default_factory=list)


def load_object(path: Path, description: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ValueError(f"cannot read {description} {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON in {description} {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{description} must contain one JSON object: {path}")
    return value


def parse_scaled(value: Any, decimals: int, field_name: str) -> int:
    """Convert a decimal string to an exact integer without using floating point."""
    if not isinstance(value, str) or not value:
        raise ValueError(f"{field_name} must be a non-empty string")
    if value[0] in "+-":
        raise ValueError(f"{field_name} must be positive")
    if value.count(".") > 1:
        raise ValueError(f"{field_name} has more than one decimal point")

    whole, separator, fraction = value.partition(".")
    if not whole or not whole.isdigit():
        raise ValueError(f"{field_name} has an invalid whole-number part")
    if separator and (not fraction or not fraction.isdigit()):
        raise ValueError(f"{field_name} has an invalid fractional part")
    if len(fraction) > decimals:
        raise ValueError(f"{field_name} has more than {decimals} decimal places")

    units = int(whole) * (10**decimals)
    if fraction:
        units += int(fraction.ljust(decimals, "0"))
    return units


def parse_positive_integer(value: Any, field_name: str) -> int:
    if not isinstance(value, str) or not value.isdigit():
        raise ValueError(f"{field_name} must be a decimal digit string")
    parsed = int(value)
    if parsed <= 0:
        raise ValueError(f"{field_name} must be positive")
    return parsed


def side_from_order_type(value: Any) -> str:
    if value == 0:
        return "buy"
    if value == 1:
        return "sell"
    raise ValueError(f"order_type must be 0 (buy) or 1 (sell), got {value!r}")


def add_to_level(levels: Counter, price_ticks: int, quantity_units: int) -> None:
    levels[price_ticks] += quantity_units


def remove_from_level(
    levels: Counter,
    price_ticks: int,
    quantity_units: int,
    errors: list[str],
    context: str,
) -> None:
    remaining = levels[price_ticks] - quantity_units
    if remaining < 0:
        errors.append(f"{context}: aggregate quantity would become negative")
        return
    if remaining == 0:
        del levels[price_ticks]
    else:
        levels[price_ticks] = remaining


def best_prices(
    buy_levels: Counter, sell_levels: Counter
) -> tuple[Optional[int], Optional[int]]:
    best_bid = max(buy_levels) if buy_levels else None
    best_ask = min(sell_levels) if sell_levels else None
    return best_bid, best_ask


def seed_snapshot(
    snapshot: dict[str, Any], result: AuditResult
) -> tuple[dict[int, RestingOrder], Counter, Counter]:
    try:
        result.snapshot_microtimestamp = parse_positive_integer(
            snapshot["microtimestamp"], "snapshot.microtimestamp"
        )
    except (KeyError, ValueError) as exc:
        raise ValueError(f"invalid snapshot microtimestamp: {exc}") from exc

    orders: dict[int, RestingOrder] = {}
    buy_levels: Counter = Counter()
    sell_levels: Counter = Counter()

    for key, side, levels in (
        ("bids", "buy", buy_levels),
        ("asks", "sell", sell_levels),
    ):
        rows = snapshot.get(key)
        if not isinstance(rows, list):
            raise ValueError(f"snapshot.{key} must be a list")
        for row_number, row in enumerate(rows, start=1):
            context = f"snapshot.{key}[{row_number}]"
            if not isinstance(row, list) or len(row) < 3:
                result.errors.append(f"{context}: expected [price, quantity, order_id]")
                continue
            try:
                price_ticks = parse_scaled(row[0], PRICE_DECIMALS, f"{context}.price")
                quantity_units = parse_scaled(
                    row[1], QUANTITY_DECIMALS, f"{context}.quantity"
                )
                order_id = parse_positive_integer(row[2], f"{context}.order_id")
            except ValueError as exc:
                result.errors.append(str(exc))
                continue
            if price_ticks <= 0 or quantity_units <= 0:
                result.errors.append(f"{context}: price and quantity must be positive")
                continue
            if order_id in orders:
                result.errors.append(f"{context}: duplicate order ID {order_id}")
                continue

            order = RestingOrder(order_id, side, price_ticks, quantity_units)
            orders[order_id] = order
            add_to_level(levels, price_ticks, quantity_units)

    result.snapshot_orders = len(orders)
    result.snapshot_price_levels = len(buy_levels) + len(sell_levels)
    result.initial_best_bid, result.initial_best_ask = best_prices(
        buy_levels, sell_levels
    )
    if (
        result.initial_best_bid is not None
        and result.initial_best_ask is not None
        and result.initial_best_bid >= result.initial_best_ask
    ):
        result.errors.append("snapshot is crossed: best bid is not below best ask")
    return orders, buy_levels, sell_levels


def parse_event_order(
    message: dict[str, Any], line_number: int
) -> tuple[RestingOrder, int]:
    data = message.get("data")
    if not isinstance(data, dict):
        raise ValueError(f"line {line_number}: missing data object")
    context = f"line {line_number}"
    order_id = parse_positive_integer(data.get("id_str"), f"{context}.id_str")
    side = side_from_order_type(data.get("order_type"))
    price_ticks = parse_scaled(
        data.get("price_str"), PRICE_DECIMALS, f"{context}.price_str"
    )
    quantity_units = parse_scaled(
        data.get("amount_str"), QUANTITY_DECIMALS, f"{context}.amount_str"
    )
    microtimestamp = parse_positive_integer(
        data.get("microtimestamp"), f"{context}.microtimestamp"
    )
    if price_ticks < 0 or quantity_units < 0:
        raise ValueError(f"{context}: price and quantity must be non-negative")
    return RestingOrder(order_id, side, price_ticks, quantity_units), microtimestamp


def replay_payload(
    result: AuditResult,
    orders: dict[int, RestingOrder],
    buy_levels: Counter,
    sell_levels: Counter,
) -> None:
    previous_event_id: Optional[str] = None
    zero_price_orders: set[int] = set()

    def record_crossed_state(line_number: int) -> None:
        best_bid, best_ask = best_prices(buy_levels, sell_levels)
        if best_bid is not None and best_ask is not None and best_bid >= best_ask:
            result.crossed_after_lines.append(line_number)

    try:
        handle = result.payload_path.open(encoding="utf-8")
    except OSError as exc:
        result.errors.append(f"cannot read payload {result.payload_path}: {exc}")
        return

    with handle:
        for line_number, raw_line in enumerate(handle, start=1):
            result.payload_lines = line_number
            try:
                message = json.loads(raw_line)
            except json.JSONDecodeError as exc:
                result.errors.append(f"line {line_number}: invalid JSON: {exc}")
                continue
            if not isinstance(message, dict):
                result.errors.append(f"line {line_number}: message is not an object")
                continue

            event = message.get("event", "<missing>")
            result.counts[event] += 1
            if event not in ORDER_EVENTS:
                # A format-v2 joined capture interleaves live_trades with the raw
                # order stream. This audit intentionally reconstructs only the
                # order book, so trades are retained as evidence for Replay but do
                # not belong in this order-only reference calculation.
                if event not in (
                    "bts:subscription_succeeded",
                    "bts:request_reconnect",
                    "trade",
                ):
                    result.errors.append(f"line {line_number}: unexpected event {event!r}")
                continue

            result.order_events += 1
            event_id = message.get("event_id")
            pre_event_id = message.get("pre_event_id")
            if previous_event_id is not None and pre_event_id != previous_event_id:
                result.errors.append(
                    f"line {line_number}: chain break; expected predecessor "
                    f"{previous_event_id!r}, got {pre_event_id!r}"
                )
                result.chain_break_line = line_number
                # ADR 0006: the event exposing a broken chain is raw evidence, but
                # it is not applied to the valid book prefix.
                break
            if isinstance(event_id, str):
                previous_event_id = event_id
            else:
                result.errors.append(f"line {line_number}: missing event_id")

            try:
                incoming, microtimestamp = parse_event_order(message, line_number)
            except ValueError as exc:
                result.errors.append(str(exc))
                continue

            if microtimestamp <= result.snapshot_microtimestamp:
                result.events_at_or_before_snapshot += 1
                continue

            levels = buy_levels if incoming.side == "buy" else sell_levels
            if event == "order_created":
                if incoming.quantity_units <= 0:
                    result.errors.append(
                        f"line {line_number}: created order has non-positive quantity"
                    )
                    continue
                if incoming.order_id in orders or incoming.order_id in zero_price_orders:
                    result.errors.append(
                        f"line {line_number}: duplicate create for order {incoming.order_id}"
                    )
                    continue
                if incoming.price_ticks == 0:
                    # The corpus contains price-zero market-order lifecycles. They
                    # never represent resting levels, so retain only the ID needed
                    # to recognize the matching change/delete event.
                    zero_price_orders.add(incoming.order_id)
                    result.zero_price_lifecycle_events += 1
                    continue
                orders[incoming.order_id] = incoming
                add_to_level(levels, incoming.price_ticks, incoming.quantity_units)
                result.applied_events += 1
                record_crossed_state(line_number)
                continue

            if incoming.order_id in zero_price_orders:
                result.zero_price_lifecycle_events += 1
                if event == "order_deleted":
                    zero_price_orders.remove(incoming.order_id)
                continue

            stored = orders.get(incoming.order_id)
            if stored is None:
                result.unknown_orders.append(
                    UnknownOrder(
                        line_number,
                        event,
                        incoming.order_id,
                        microtimestamp,
                    )
                )
                continue
            if incoming.side != stored.side:
                result.errors.append(
                    f"line {line_number}: {event} side disagrees with stored order "
                    f"{incoming.order_id}"
                )
                continue
            if event == "order_changed" and incoming.price_ticks != stored.price_ticks:
                result.changed_price_moves += 1
            if event == "order_deleted" and incoming.price_ticks != stored.price_ticks:
                # A Bitstamp delete can carry a replacement or execution price. The
                # resting order's stored price identifies the level that must be changed.
                result.deletion_price_differences += 1

            stored_levels = buy_levels if stored.side == "buy" else sell_levels
            if event == "order_changed":
                if incoming.quantity_units <= 0:
                    result.errors.append(
                        f"line {line_number}: changed order has non-positive quantity"
                    )
                    continue
                remove_from_level(
                    stored_levels,
                    stored.price_ticks,
                    stored.quantity_units,
                    result.errors,
                    f"line {line_number}",
                )
                orders[incoming.order_id] = incoming
                add_to_level(levels, incoming.price_ticks, incoming.quantity_units)
            else:
                remove_from_level(
                    stored_levels,
                    stored.price_ticks,
                    stored.quantity_units,
                    result.errors,
                    f"line {line_number}",
                )
                del orders[incoming.order_id]
            result.applied_events += 1
            record_crossed_state(line_number)


def check_final_invariants(
    result: AuditResult,
    orders: dict[int, RestingOrder],
    buy_levels: Counter,
    sell_levels: Counter,
) -> None:
    expected_buy: Counter = Counter()
    expected_sell: Counter = Counter()
    for order in orders.values():
        levels = expected_buy if order.side == "buy" else expected_sell
        levels[order.price_ticks] += order.quantity_units

    if expected_buy != buy_levels:
        result.errors.append("final buy-level totals disagree with stored orders")
    if expected_sell != sell_levels:
        result.errors.append("final sell-level totals disagree with stored orders")

    result.final_orders = len(orders)
    result.final_price_levels = len(buy_levels) + len(sell_levels)
    result.final_best_bid, result.final_best_ask = best_prices(buy_levels, sell_levels)
    if (
        result.chain_break_line is None
        and result.final_best_bid is not None
        and result.final_best_ask is not None
        and result.final_best_bid >= result.final_best_ask
    ):
        result.errors.append("final book is crossed: best bid is not below best ask")


def audit_segment(snapshot_path: Path, payload_path: Path) -> AuditResult:
    result = AuditResult(snapshot_path=snapshot_path, payload_path=payload_path)
    try:
        snapshot = load_object(snapshot_path, "snapshot")
        orders, buy_levels, sell_levels = seed_snapshot(snapshot, result)
    except ValueError as exc:
        result.errors.append(str(exc))
        return result
    replay_payload(result, orders, buy_levels, sell_levels)
    check_final_invariants(result, orders, buy_levels, sell_levels)
    return result


def format_price(ticks: Optional[int]) -> str:
    if ticks is None:
        return "empty"
    return f"{ticks // 100}.{ticks % 100:02d}"


def print_result(result: AuditResult) -> None:
    print(f"snapshot             : {result.snapshot_path}")
    print(f"payload              : {result.payload_path}")
    print(f"snapshot micro       : {result.snapshot_microtimestamp}")
    print(f"snapshot orders      : {result.snapshot_orders:,}")
    print(f"snapshot levels      : {result.snapshot_price_levels:,}")
    print(
        f"initial best bid/ask : {format_price(result.initial_best_bid)} / "
        f"{format_price(result.initial_best_ask)}"
    )
    print(f"payload lines        : {result.payload_lines:,}")
    print(f"order events         : {result.order_events:,}")
    print(f"events <= snapshot   : {result.events_at_or_before_snapshot:,}")
    print(f"events applied       : {result.applied_events:,}")
    print(f"unknown modify/delete: {len(result.unknown_orders):,}")
    print(f"changed price moves   : {result.changed_price_moves:,}")
    print(f"delete price differs : {result.deletion_price_differences:,}")
    print(f"zero-price lifecycle : {result.zero_price_lifecycle_events:,}")
    print(f"crossed after event  : {len(result.crossed_after_lines):,}")
    if result.crossed_after_lines:
        print(
            f"crossed event range   : lines {result.crossed_after_lines[0]:,}.."
            f"{result.crossed_after_lines[-1]:,}"
        )
        print(
            "crossing note         : live_orders exposes marketable order lifecycles; "
            "a raw created event is not always a settled resting-book update"
        )
    if result.chain_break_line is not None:
        print(f"chain boundary       : line {result.chain_break_line:,}; replay stopped")
    if result.unknown_orders:
        first = result.unknown_orders[0]
        last = result.unknown_orders[-1]
        print(
            f"unknown range        : lines {first.line:,}..{last.line:,} "
            "(observed warm-up evidence)"
        )
        for unknown in result.unknown_orders[:10]:
            print(
                f"  line {unknown.line:,}: {unknown.event} "
                f"unknown ID {unknown.order_id}"
            )
        if len(result.unknown_orders) > 10:
            print(f"  ... and {len(result.unknown_orders) - 10:,} more")
    print(f"final orders         : {result.final_orders:,}")
    print(f"final levels         : {result.final_price_levels:,}")
    print(
        f"final best bid/ask   : {format_price(result.final_best_bid)} / "
        f"{format_price(result.final_best_ask)}"
    )
    if result.errors:
        print(f"RESULT               : INVALID ({len(result.errors)} hard errors)")
        for error in result.errors[:20]:
            print(f"  {error}")
        if len(result.errors) > 20:
            print(f"  ... and {len(result.errors) - 20} more")
    elif result.unknown_orders:
        print(
            "RESULT               : CONSISTENT REPLAY WITH STARTUP-BOUNDARY EVIDENCE"
        )
        print(
            "NOTE                 : the suffix after the last unknown event is "
            "internally consistent; this alone does not prove snapshot FIFO order."
        )
    else:
        print("RESULT               : CONSISTENT REPLAY")
    print()


def paths_from_manifest(manifest_path: Path) -> list[tuple[Path, Path]]:
    manifest = load_object(manifest_path, "manifest")
    segments = manifest.get("segments")
    if not isinstance(segments, list) or not segments:
        raise ValueError("manifest must contain a non-empty segments list")
    result: list[tuple[Path, Path]] = []
    for position, segment in enumerate(segments):
        if not isinstance(segment, dict):
            raise ValueError(f"manifest segment {position} is not an object")
        snapshot = segment.get("snapshot")
        payload = segment.get("payload")
        if (
            not isinstance(snapshot, str)
            or Path(snapshot).name != snapshot
            or not isinstance(payload, str)
            or Path(payload).name != payload
        ):
            raise ValueError(f"manifest segment {position} has unsafe file names")
        result.append((manifest_path.parent / snapshot, manifest_path.parent / payload))
    return result


def resolve_paths(arguments: list[str]) -> list[tuple[Path, Path]]:
    if len(arguments) == 1:
        path = Path(arguments[0])
        manifest_path = path / "manifest.json" if path.is_dir() else path
        if manifest_path.name != "manifest.json":
            raise ValueError("one argument must be a capture directory or manifest.json")
        return paths_from_manifest(manifest_path)
    if len(arguments) == 2:
        return [(Path(arguments[0]), Path(arguments[1]))]
    raise ValueError("expected a capture directory/manifest or snapshot + payload paths")


def main(arguments: list[str]) -> int:
    try:
        pairs = resolve_paths(arguments)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        print(__doc__, file=sys.stderr)
        return 2

    has_errors = False
    for snapshot_path, payload_path in pairs:
        result = audit_segment(snapshot_path, payload_path)
        print_result(result)
        has_errors = has_errors or bool(result.errors)
    return 1 if has_errors else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
