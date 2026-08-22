"""Validate a Bitstamp capture against ADR 0006.

The preferred input is a capture directory or its manifest.json. A legacy standalone
JSONL file is also accepted for validating an existing single segment.

Usage:
    python scripts/validate_capture.py data/raw/bitstamp-btcusd-<timestamp>
    python scripts/validate_capture.py data/raw/bitstamp-btcusd-<timestamp>/manifest.json
    python scripts/validate_capture.py data/raw/btcusd-live-orders.jsonl
"""

import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional


ORDER_EVENTS = {"order_created", "order_changed", "order_deleted"}
EVENT_ID_PATTERN = re.compile(
    r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
    r"[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
)


@dataclass
class SegmentResult:
    path: Path
    lines: int = 0
    order_events: int = 0
    first_micro: Optional[int] = None
    last_micro: Optional[int] = None
    counts: Counter = field(default_factory=Counter)
    problems: list[str] = field(default_factory=list)
    reconnect_line: Optional[int] = None

    @property
    def span_seconds(self) -> float:
        if self.first_micro is None or self.last_micro is None:
            return 0.0
        return (self.last_micro - self.first_micro) / 1e6


def load_json(path: Path, description: str) -> tuple[Optional[dict[str, Any]], list[str]]:
    try:
        value = json.loads(path.read_bytes())
    except FileNotFoundError:
        return None, [f"missing {description}: {path}"]
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        return None, [f"invalid {description} {path}: {exc}"]
    if not isinstance(value, dict):
        return None, [f"{description} must contain one JSON object: {path}"]
    return value, []


def validate_snapshot(path: Path) -> tuple[Optional[int], list[str]]:
    snapshot, problems = load_json(path, "snapshot")
    if snapshot is None:
        return None, problems
    try:
        microtimestamp = int(snapshot["microtimestamp"])
    except (KeyError, TypeError, ValueError):
        problems.append(f"snapshot has invalid or missing microtimestamp: {path}")
        return None, problems
    for side in ("bids", "asks"):
        if not isinstance(snapshot.get(side), list):
            problems.append(f"snapshot has invalid or missing {side}: {path}")
    return microtimestamp, problems


def validate_segment(path: Path) -> SegmentResult:
    result = SegmentResult(path=path)
    previous_event_id: Optional[str] = None
    previous_line_no: Optional[int] = None
    subscription_lines: list[int] = []

    try:
        handle = path.open(encoding="utf-8")
    except OSError as exc:
        result.problems.append(f"cannot open payload {path}: {exc}")
        return result

    with handle:
        for line_no, raw_line in enumerate(handle, start=1):
            result.lines = line_no
            line = raw_line.strip()
            if not line:
                result.problems.append(f"line {line_no}: blank payload record")
                continue
            try:
                message = json.loads(line)
            except json.JSONDecodeError as exc:
                result.problems.append(f"line {line_no}: invalid JSON: {exc}")
                continue
            if not isinstance(message, dict):
                result.problems.append(f"line {line_no}: payload is not a JSON object")
                continue

            event = message.get("event", "<missing>")
            result.counts[event] += 1

            if event == "bts:subscription_succeeded":
                subscription_lines.append(line_no)
                if message.get("channel") not in (None, "live_orders_btcusd"):
                    result.problems.append(
                        f"line {line_no}: unexpected subscription channel "
                        f"{message.get('channel')!r}"
                    )
                continue
            if event == "bts:error":
                result.problems.append(f"line {line_no}: venue returned bts:error")
                continue
            if event == "bts:request_reconnect":
                if result.reconnect_line is not None:
                    result.problems.append(
                        f"line {line_no}: repeated bts:request_reconnect"
                    )
                result.reconnect_line = line_no
                continue
            if event not in ORDER_EVENTS:
                result.problems.append(f"line {line_no}: unexpected event {event!r}")
                continue
            if result.reconnect_line is not None:
                result.problems.append(
                    f"line {line_no}: order event appears after bts:request_reconnect"
                )

            event_id = message.get("event_id")
            pre_event_id = message.get("pre_event_id")
            if not isinstance(event_id, str) or not EVENT_ID_PATTERN.fullmatch(event_id):
                result.problems.append(f"line {line_no}: invalid or missing event_id")
            if not isinstance(pre_event_id, str) or not EVENT_ID_PATTERN.fullmatch(
                pre_event_id
            ):
                result.problems.append(f"line {line_no}: invalid or missing pre_event_id")

            data = message.get("data")
            if not isinstance(data, dict):
                result.problems.append(f"line {line_no}: missing data object")
                continue
            try:
                micro = int(data["microtimestamp"])
            except (KeyError, TypeError, ValueError):
                result.problems.append(
                    f"line {line_no}: invalid or missing microtimestamp"
                )
                continue

            if result.last_micro is not None and micro < result.last_micro:
                result.problems.append(
                    f"line {line_no}: microtimestamp moved backwards "
                    f"({micro} < {result.last_micro})"
                )
            result.first_micro = result.first_micro or micro
            result.last_micro = micro
            result.order_events += 1

            if (
                previous_event_id is not None
                and isinstance(pre_event_id, str)
                and pre_event_id != previous_event_id
            ):
                result.problems.append(
                    f"line {line_no}: chain break after line {previous_line_no}; "
                    f"expected {previous_event_id}, got {pre_event_id}"
                )
            if isinstance(event_id, str):
                previous_event_id = event_id
                previous_line_no = line_no

    if subscription_lines != [1]:
        result.problems.append(
            "subscription acknowledgement must occur exactly once on line 1; "
            f"found lines {subscription_lines}"
        )
    if result.reconnect_line is not None and result.reconnect_line != result.lines:
        result.problems.append(
            f"bts:request_reconnect on line {result.reconnect_line} is not the final line"
        )
    if result.order_events == 0:
        result.problems.append("segment contains no order events")
    return result


def print_segment(result: SegmentResult, snapshot_micro: Optional[int] = None) -> None:
    print(f"segment         : {result.path}")
    if result.path.exists():
        print(f"bytes           : {result.path.stat().st_size:,}")
    print(f"lines           : {result.lines:,}")
    print(f"order events    : {result.order_events:,}")
    print(
        f"span            : {result.span_seconds:,.1f} s "
        f"({result.span_seconds / 60:.1f} min)"
    )
    if result.span_seconds:
        print(f"rate            : {result.order_events / result.span_seconds:.1f} events/s")
    if snapshot_micro is not None:
        print(f"snapshot micro  : {snapshot_micro}")
        before = (
            result.first_micro is not None and result.first_micro <= snapshot_micro
        )
        print(f"buffered before : {'yes' if before else 'no'}")
    for event, count in result.counts.most_common():
        print(f"  {event:<32} {count:>9,}")
    if result.problems:
        print(f"PROBLEMS        : {len(result.problems)}")
        for problem in result.problems[:20]:
            print(f"  {problem}")
        if len(result.problems) > 20:
            print(f"  ... and {len(result.problems) - 20} more")
    else:
        print("RESULT          : valid continuous segment")
    print()


def validate_manifest(manifest_path: Path) -> int:
    manifest, problems = load_json(manifest_path, "manifest")
    if manifest is None:
        for problem in problems:
            print(problem)
        return 1

    if manifest.get("format_version") == 2:
        try:
            from validate_joined_capture import validate_manifest as validate_joined_manifest
        except ModuleNotFoundError:
            from scripts.validate_joined_capture import (
                validate_manifest as validate_joined_manifest,
            )
        return validate_joined_manifest(manifest_path)

    root = manifest_path.parent
    if manifest.get("format_version") != 1:
        problems.append(f"unsupported format_version {manifest.get('format_version')!r}")
    if manifest.get("status") != "completed":
        problems.append(
            f"capture status is {manifest.get('status', '<missing>')!r}, not 'completed'"
        )
    if manifest.get("venue") != "bitstamp":
        problems.append(f"unexpected venue {manifest.get('venue')!r}")
    if manifest.get("instrument") != "btcusd":
        problems.append(f"unexpected instrument {manifest.get('instrument')!r}")
    if manifest.get("channel") != "live_orders_btcusd":
        problems.append(f"unexpected channel {manifest.get('channel')!r}")

    segments = manifest.get("segments")
    if not isinstance(segments, list) or not segments:
        problems.append("manifest must contain a non-empty segments list")
        segments = []

    total_events = 0
    total_span = 0.0
    for expected_index, segment in enumerate(segments):
        if not isinstance(segment, dict):
            problems.append(f"segment {expected_index}: manifest entry is not an object")
            continue
        if segment.get("index") != expected_index:
            problems.append(
                f"segment {expected_index}: index is {segment.get('index')!r}"
            )
        payload_name = segment.get("payload")
        snapshot_name = segment.get("snapshot")
        if not isinstance(payload_name, str) or Path(payload_name).name != payload_name:
            problems.append(f"segment {expected_index}: unsafe or missing payload path")
            continue
        if not isinstance(snapshot_name, str) or Path(snapshot_name).name != snapshot_name:
            problems.append(f"segment {expected_index}: unsafe or missing snapshot path")
            continue

        snapshot_micro, snapshot_problems = validate_snapshot(root / snapshot_name)
        problems.extend(snapshot_problems)
        snapshot_metadata = segment.get("snapshot_metadata")
        if not isinstance(snapshot_metadata, dict):
            problems.append(f"segment {expected_index}: missing snapshot_metadata")
        elif snapshot_micro is not None and str(snapshot_micro) != str(
            snapshot_metadata.get("microtimestamp")
        ):
            problems.append(
                f"segment {expected_index}: snapshot microtimestamp does not match manifest"
            )
        result = validate_segment(root / payload_name)
        print_segment(result, snapshot_micro)
        problems.extend(
            f"segment {expected_index}: {problem}" for problem in result.problems
        )
        total_events += result.order_events
        total_span += result.span_seconds

        if segment.get("messages") is not None and segment.get("messages") != result.lines:
            problems.append(
                f"segment {expected_index}: manifest messages={segment.get('messages')} "
                f"but file has {result.lines} lines"
            )
        if (
            segment.get("order_events") is not None
            and segment.get("order_events") != result.order_events
        ):
            problems.append(
                f"segment {expected_index}: manifest order_events="
                f"{segment.get('order_events')} but file has {result.order_events}"
            )
        reason = segment.get("end_reason")
        if reason == "venue_requested_reconnect" and result.reconnect_line is None:
            problems.append(
                f"segment {expected_index}: manifest says requested reconnect but payload does not"
            )
        if result.reconnect_line is not None and reason != "venue_requested_reconnect":
            problems.append(
                f"segment {expected_index}: payload requests reconnect but end_reason={reason!r}"
            )
        if segment.get("chain_valid") is False:
            problems.append(f"segment {expected_index}: capture detected a chain gap")

    print(f"manifest        : {manifest_path}")
    print(f"status          : {manifest.get('status', '<missing>')}")
    print(f"segments        : {len(segments):,}")
    print(f"order events    : {total_events:,}")
    print(f"combined span   : {total_span:,.1f} s ({total_span / 60:.1f} min)")
    if problems:
        print(f"MANIFEST RESULT : INVALID ({len(problems)} problems)")
        for problem in problems[:30]:
            print(f"  {problem}")
        if len(problems) > 30:
            print(f"  ... and {len(problems) - 30} more")
        return 1
    print("MANIFEST RESULT : VALID")
    return 0


def validate_legacy(path: Path) -> int:
    result = validate_segment(path)
    snapshot_path = path.with_suffix(".snapshot")
    snapshot_micro = None
    snapshot_problems: list[str] = []
    if snapshot_path.exists():
        snapshot_micro, snapshot_problems = validate_snapshot(snapshot_path)
        result.problems.extend(snapshot_problems)
    print_segment(result, snapshot_micro)
    if not snapshot_path.exists():
        print("NOTE            : legacy JSONL validated without a bound snapshot/manifest")
    return 1 if result.problems else 0


def main(path: Path) -> int:
    if path.is_dir():
        return validate_manifest(path / "manifest.json")
    if path.name == "manifest.json":
        return validate_manifest(path)
    return validate_legacy(path)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        raise SystemExit(2)
    raise SystemExit(main(Path(sys.argv[1])))
