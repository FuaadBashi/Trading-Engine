"""Validate a format-v2 Bitstamp joined order/trade capture.

The capture keeps raw WebSocket text payloads in ``segment-NNNN.jsonl`` and
stores provenance separately in ``segment-NNNN.frames.jsonl``. This validator
proves those files still agree: one consecutive capture ordinal identifies every
raw line, order-chain checks apply only to the order stream, and hashes/sizes in
the manifest match the bytes on disk.
"""

import hashlib
import json
import re
from collections import Counter
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any, Optional


ORDER_CHANNEL = "live_orders_btcusd"
TRADE_CHANNEL = "live_trades_btcusd"
CHANNELS = {ORDER_CHANNEL, TRADE_CHANNEL}
ORDER_EVENTS = {"order_created", "order_changed", "order_deleted"}
EVENT_ID_PATTERN = re.compile(
    r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
    r"[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
)
FRAME_FIELDS = {
    "captureOrdinal",
    "localWallTimestampNanos",
    "localSteadyTimestampNanos",
    "streamKind",
    "venueTimestampMicros",
    "runId",
    "segmentId",
    "payloadLine",
}


@dataclass
class JoinedSegmentResult:
    payload_path: Path
    frames_path: Path
    payload_lines: int = 0
    frame_lines: int = 0
    order_events: int = 0
    trade_events: int = 0
    control_frames: int = 0
    first_capture_ordinal: Optional[int] = None
    last_capture_ordinal: Optional[int] = None
    first_order_microtimestamp: Optional[int] = None
    last_order_microtimestamp: Optional[int] = None
    first_trade_microtimestamp: Optional[int] = None
    last_trade_microtimestamp: Optional[int] = None
    first_event_id: Optional[str] = None
    last_event_id: Optional[str] = None
    reconnect_line: Optional[int] = None
    subscription_counts: Counter = field(default_factory=Counter)
    problems: list[str] = field(default_factory=list)


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


def sha256_file(path: Path) -> Optional[str]:
    try:
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest()
    except OSError:
        return None


def parse_manifest_time(
    value: Any, description: str, problems: list[str]
) -> Optional[datetime]:
    if not isinstance(value, str):
        problems.append(f"{description} is missing or is not a string")
        return None
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        problems.append(f"{description} is not a valid ISO-8601 timestamp")
        return None
    if parsed.tzinfo is None:
        problems.append(f"{description} has no timezone")
        return None
    return parsed


def validate_snapshot_record(
    path: Path, metadata: Any, description: str
) -> tuple[Optional[int], list[str]]:
    microtimestamp, problems = validate_snapshot(path)
    if not isinstance(metadata, dict):
        problems.append(f"{description} is missing metadata")
        return microtimestamp, problems

    if microtimestamp is not None and str(metadata.get("microtimestamp")) != str(
        microtimestamp
    ):
        problems.append(f"{description} microtimestamp does not match its file")

    try:
        actual_bytes = path.stat().st_size
    except OSError:
        actual_bytes = None
    if metadata.get("bytes") != actual_bytes:
        problems.append(
            f"{description} bytes={metadata.get('bytes')!r}, expected {actual_bytes!r}"
        )

    actual_sha256 = sha256_file(path)
    if metadata.get("sha256") != actual_sha256:
        problems.append(f"{description} SHA-256 does not match its file")

    requested_at = parse_manifest_time(
        metadata.get("requested_at"), f"{description} requested_at", problems
    )
    received_at = parse_manifest_time(
        metadata.get("received_at"), f"{description} received_at", problems
    )
    if (
        requested_at is not None
        and received_at is not None
        and received_at < requested_at
    ):
        problems.append(f"{description} received_at is before requested_at")
    return microtimestamp, problems


def is_positive_integer(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def stream_metadata(message: dict[str, Any]) -> tuple[str, Optional[str]]:
    event = message.get("event")
    channel = message.get("channel")
    data = message.get("data")
    timestamp = None
    if isinstance(data, dict) and data.get("microtimestamp") is not None:
        timestamp = str(data["microtimestamp"])
    if event in ORDER_EVENTS and channel == ORDER_CHANNEL:
        return "order", timestamp
    if event == "trade" and channel == TRADE_CHANNEL:
        return "trade", timestamp
    return "control", timestamp


def parse_microtimestamp(
    value: Optional[str], line_no: int, result: JoinedSegmentResult, kind: str
) -> Optional[int]:
    if value is None:
        result.problems.append(f"line {line_no}: {kind} event has no microtimestamp")
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        result.problems.append(
            f"line {line_no}: {kind} event has invalid microtimestamp {value!r}"
        )
        return None


def validate_order_event(
    message: dict[str, Any],
    line_no: int,
    result: JoinedSegmentResult,
    previous_event_id: Optional[str],
) -> Optional[str]:
    event_id = message.get("event_id")
    pre_event_id = message.get("pre_event_id")
    if not isinstance(event_id, str) or not EVENT_ID_PATTERN.fullmatch(event_id):
        result.problems.append(f"line {line_no}: invalid or missing event_id")
    if not isinstance(pre_event_id, str) or not EVENT_ID_PATTERN.fullmatch(pre_event_id):
        result.problems.append(f"line {line_no}: invalid or missing pre_event_id")
    if (
        previous_event_id is not None
        and isinstance(pre_event_id, str)
        and pre_event_id != previous_event_id
    ):
        result.problems.append(
            f"line {line_no}: chain break; expected {previous_event_id}, got {pre_event_id}"
        )
    return event_id if isinstance(event_id, str) else previous_event_id


def validate_joined_segment(
    payload_path: Path,
    frames_path: Path,
    run_id: str,
    segment_id: int,
    previous_capture_ordinal: int,
) -> JoinedSegmentResult:
    result = JoinedSegmentResult(payload_path=payload_path, frames_path=frames_path)
    previous_event_id: Optional[str] = None
    expected_capture_ordinal = previous_capture_ordinal + 1

    try:
        payload_source = payload_path.open(encoding="utf-8")
    except OSError as exc:
        result.problems.append(f"cannot open payload {payload_path}: {exc}")
        return result
    try:
        frames_source = frames_path.open(encoding="utf-8")
    except OSError as exc:
        payload_source.close()
        result.problems.append(f"cannot open frame index {frames_path}: {exc}")
        return result

    with payload_source, frames_source:
        while True:
            raw_payload = payload_source.readline()
            raw_frame = frames_source.readline()
            if not raw_payload and not raw_frame:
                break

            if raw_payload:
                result.payload_lines += 1
            if raw_frame:
                result.frame_lines += 1
            line_no = max(result.payload_lines, result.frame_lines)
            if not raw_payload:
                result.problems.append(f"line {line_no}: frame index has no payload line")
                continue
            if not raw_frame:
                result.problems.append(f"line {line_no}: payload has no frame-index line")
                continue
            if not raw_payload.endswith("\n"):
                result.problems.append(
                    f"line {line_no}: payload line is not newline terminated"
                )
            if not raw_frame.endswith("\n"):
                result.problems.append(
                    f"line {line_no}: frame-index line is not newline terminated"
                )

            try:
                message = json.loads(raw_payload)
            except json.JSONDecodeError as exc:
                result.problems.append(f"line {line_no}: invalid payload JSON: {exc}")
                continue
            if not isinstance(message, dict):
                result.problems.append(f"line {line_no}: payload is not a JSON object")
                continue
            try:
                frame = json.loads(raw_frame)
            except json.JSONDecodeError as exc:
                result.problems.append(f"line {line_no}: invalid frame-index JSON: {exc}")
                continue
            if not isinstance(frame, dict):
                result.problems.append(f"line {line_no}: frame index is not a JSON object")
                continue

            missing = FRAME_FIELDS.difference(frame)
            if missing:
                result.problems.append(
                    f"line {line_no}: frame is missing fields {sorted(missing)!r}"
                )
                continue
            ordinal = frame["captureOrdinal"]
            if not is_positive_integer(ordinal):
                result.problems.append(f"line {line_no}: invalid captureOrdinal {ordinal!r}")
            elif ordinal != expected_capture_ordinal:
                result.problems.append(
                    f"line {line_no}: captureOrdinal {ordinal} does not follow "
                    f"{expected_capture_ordinal - 1}"
                )
                expected_capture_ordinal = ordinal + 1
            else:
                expected_capture_ordinal += 1
            if is_positive_integer(ordinal):
                result.first_capture_ordinal = result.first_capture_ordinal or ordinal
                result.last_capture_ordinal = ordinal

            for field_name in ("localWallTimestampNanos", "localSteadyTimestampNanos"):
                if not is_positive_integer(frame[field_name]):
                    result.problems.append(
                        f"line {line_no}: invalid {field_name} {frame[field_name]!r}"
                    )
            if frame["runId"] != run_id:
                result.problems.append(f"line {line_no}: frame runId does not match manifest")
            if frame["segmentId"] != segment_id:
                result.problems.append(f"line {line_no}: frame segmentId does not match manifest")
            if frame["payloadLine"] != line_no:
                result.problems.append(
                    f"line {line_no}: frame payloadLine is {frame['payloadLine']!r}"
                )

            expected_kind, expected_micro = stream_metadata(message)
            if expected_kind == "control":
                result.control_frames += 1
            if frame["streamKind"] not in {"order", "trade", "control"}:
                result.problems.append(
                    f"line {line_no}: invalid streamKind {frame['streamKind']!r}"
                )
            elif frame["streamKind"] != expected_kind:
                result.problems.append(
                    f"line {line_no}: frame streamKind {frame['streamKind']!r} "
                    f"does not match payload {expected_kind!r}"
                )
            if frame["venueTimestampMicros"] != expected_micro:
                result.problems.append(
                    f"line {line_no}: frame venueTimestampMicros does not match payload"
                )

            event = message.get("event")
            channel = message.get("channel")
            if event == "bts:subscription_succeeded":
                if channel not in CHANNELS:
                    result.problems.append(
                        f"line {line_no}: unexpected subscription channel {channel!r}"
                    )
                else:
                    result.subscription_counts[channel] += 1
            elif event == "bts:error":
                result.problems.append(f"line {line_no}: venue returned bts:error")
            elif event == "bts:request_reconnect":
                if result.reconnect_line is not None:
                    result.problems.append(
                        f"line {line_no}: repeated bts:request_reconnect"
                    )
                result.reconnect_line = line_no
            elif event in ORDER_EVENTS:
                if channel != ORDER_CHANNEL:
                    result.problems.append(
                        f"line {line_no}: order event has channel {channel!r}"
                    )
                if result.reconnect_line is not None:
                    result.problems.append(
                        f"line {line_no}: order event appears after bts:request_reconnect"
                    )
                result.order_events += 1
                previous_event_id = validate_order_event(
                    message, line_no, result, previous_event_id
                )
                result.first_event_id = result.first_event_id or previous_event_id
                result.last_event_id = previous_event_id
                micro = parse_microtimestamp(expected_micro, line_no, result, "order")
                if micro is not None:
                    if (
                        result.last_order_microtimestamp is not None
                        and micro < result.last_order_microtimestamp
                    ):
                        result.problems.append(
                            f"line {line_no}: order microtimestamp moved backwards "
                            f"({micro} < {result.last_order_microtimestamp})"
                        )
                    result.first_order_microtimestamp = (
                        result.first_order_microtimestamp or micro
                    )
                    result.last_order_microtimestamp = micro
            elif event == "trade":
                if channel != TRADE_CHANNEL:
                    result.problems.append(
                        f"line {line_no}: trade event has channel {channel!r}"
                    )
                if result.reconnect_line is not None:
                    result.problems.append(
                        f"line {line_no}: trade event appears after bts:request_reconnect"
                    )
                result.trade_events += 1
                micro = parse_microtimestamp(expected_micro, line_no, result, "trade")
                if micro is not None:
                    result.first_trade_microtimestamp = (
                        result.first_trade_microtimestamp or micro
                    )
                    result.last_trade_microtimestamp = micro
            else:
                if result.reconnect_line is not None:
                    result.problems.append(
                        f"line {line_no}: control frame appears after bts:request_reconnect"
                    )

    if result.payload_lines != result.frame_lines:
        result.problems.append(
            f"payload has {result.payload_lines} lines but frame index has "
            f"{result.frame_lines}"
        )
    for channel in CHANNELS:
        if result.subscription_counts[channel] != 1:
            result.problems.append(
                f"subscription acknowledgement for {channel} occurred "
                f"{result.subscription_counts[channel]} times"
            )
    if result.reconnect_line is not None and result.reconnect_line != result.payload_lines:
        result.problems.append(
            f"bts:request_reconnect on line {result.reconnect_line} is not the final line"
        )
    if result.order_events == 0:
        result.problems.append("segment contains no order events")
    if result.trade_events == 0:
        result.problems.append("segment contains no trade events")
    return result


def safe_file_name(value: Any) -> bool:
    return isinstance(value, str) and Path(value).name == value


def compare_manifest_value(
    result: JoinedSegmentResult, segment: dict[str, Any], field_name: str, value: Any
) -> None:
    if segment.get(field_name) != value:
        result.problems.append(
            f"manifest {field_name}={segment.get(field_name)!r}, expected {value!r}"
        )


def print_segment(
    result: JoinedSegmentResult,
    snapshot_micro: Optional[int],
    checkpoint_micro: Optional[int],
) -> None:
    print(f"payload         : {result.payload_path}")
    print(f"frame index     : {result.frames_path}")
    print(f"raw frames      : {result.payload_lines:,}")
    print(f"order events    : {result.order_events:,}")
    print(f"trade events    : {result.trade_events:,}")
    print(f"control frames  : {result.control_frames:,}")
    print(
        "capture ordinal : "
        f"{result.first_capture_ordinal}..{result.last_capture_ordinal}"
    )
    if snapshot_micro is not None:
        print(f"snapshot micro  : {snapshot_micro}")
    if checkpoint_micro is not None:
        print(f"checkpoint micro: {checkpoint_micro}")
    if result.problems:
        print(f"PROBLEMS        : {len(result.problems)}")
        for problem in result.problems[:20]:
            print(f"  {problem}")
        if len(result.problems) > 20:
            print(f"  ... and {len(result.problems) - 20} more")
    else:
        print("RESULT          : valid joined continuous segment")
    print()


def validate_manifest(manifest_path: Path) -> int:
    manifest, problems = load_json(manifest_path, "manifest")
    if manifest is None:
        for problem in problems:
            print(problem)
        return 1

    root = manifest_path.parent
    if manifest.get("format_version") != 2:
        problems.append(f"unsupported format_version {manifest.get('format_version')!r}")
    if manifest.get("capture_format") != "bitstamp_joined_frames_v1":
        problems.append(f"unexpected capture_format {manifest.get('capture_format')!r}")
    if manifest.get("status") != "completed":
        problems.append(
            f"capture status is {manifest.get('status', '<missing>')!r}, not 'completed'"
        )
    if manifest.get("venue") != "bitstamp":
        problems.append(f"unexpected venue {manifest.get('venue')!r}")
    if manifest.get("instrument") != "btcusd":
        problems.append(f"unexpected instrument {manifest.get('instrument')!r}")
    if manifest.get("channels") != [ORDER_CHANNEL, TRADE_CHANNEL]:
        problems.append(f"unexpected channels {manifest.get('channels')!r}")
    run_id = manifest.get("runId")
    if not isinstance(run_id, str) or not run_id:
        problems.append("manifest has no valid runId")
        run_id = "<invalid-run-id>"

    segments = manifest.get("segments")
    if not isinstance(segments, list) or not segments:
        problems.append("manifest must contain a non-empty segments list")
        segments = []

    previous_capture_ordinal = 0
    total_orders = 0
    total_trades = 0
    total_frames = 0
    for expected_index, segment in enumerate(segments):
        if not isinstance(segment, dict):
            problems.append(f"segment {expected_index}: manifest entry is not an object")
            continue
        if segment.get("index") != expected_index:
            problems.append(
                f"segment {expected_index}: index is {segment.get('index')!r}"
            )
        payload_name = segment.get("payload")
        frames_name = segment.get("frame_index")
        snapshot_name = segment.get("snapshot")
        checkpoint_name = segment.get("checkpoint")
        if not all(
            safe_file_name(name)
            for name in (payload_name, frames_name, snapshot_name, checkpoint_name)
        ):
            problems.append(f"segment {expected_index}: unsafe or missing file name")
            continue

        payload_path = root / payload_name
        frames_path = root / frames_name
        snapshot_micro, snapshot_problems = validate_snapshot_record(
            root / snapshot_name,
            segment.get("snapshot_metadata"),
            f"segment {expected_index} start snapshot",
        )
        problems.extend(snapshot_problems)

        result = validate_joined_segment(
            payload_path, frames_path, run_id, expected_index, previous_capture_ordinal
        )
        if result.last_capture_ordinal is not None:
            previous_capture_ordinal = result.last_capture_ordinal
        total_frames += result.payload_lines
        total_orders += result.order_events
        total_trades += result.trade_events

        compare_manifest_value(result, segment, "frames", result.payload_lines)
        compare_manifest_value(result, segment, "order_events", result.order_events)
        compare_manifest_value(result, segment, "trade_events", result.trade_events)
        compare_manifest_value(result, segment, "control_frames", result.control_frames)
        compare_manifest_value(
            result, segment, "first_capture_ordinal", result.first_capture_ordinal
        )
        compare_manifest_value(
            result, segment, "last_capture_ordinal", result.last_capture_ordinal
        )
        compare_manifest_value(result, segment, "first_event_id", result.first_event_id)
        compare_manifest_value(result, segment, "last_event_id", result.last_event_id)
        compare_manifest_value(
            result,
            segment,
            "first_order_microtimestamp",
            str(result.first_order_microtimestamp)
            if result.first_order_microtimestamp is not None
            else None,
        )
        compare_manifest_value(
            result,
            segment,
            "last_order_microtimestamp",
            str(result.last_order_microtimestamp)
            if result.last_order_microtimestamp is not None
            else None,
        )
        compare_manifest_value(
            result,
            segment,
            "first_trade_microtimestamp",
            str(result.first_trade_microtimestamp)
            if result.first_trade_microtimestamp is not None
            else None,
        )
        compare_manifest_value(
            result,
            segment,
            "last_trade_microtimestamp",
            str(result.last_trade_microtimestamp)
            if result.last_trade_microtimestamp is not None
            else None,
        )
        if payload_path.exists():
            compare_manifest_value(
                result, segment, "payload_bytes", payload_path.stat().st_size
            )
        if frames_path.exists():
            compare_manifest_value(
                result, segment, "frames_bytes", frames_path.stat().st_size
            )
        compare_manifest_value(
            result, segment, "payload_sha256", sha256_file(payload_path)
        )
        compare_manifest_value(
            result, segment, "frames_sha256", sha256_file(frames_path)
        )
        if segment.get("subscriptions") != sorted(CHANNELS):
            problems.append(
                f"segment {expected_index}: missing subscription acknowledgements"
            )
        if segment.get("chain_valid") is False:
            problems.append(f"segment {expected_index}: capture detected a chain gap")
        reason = segment.get("end_reason")
        if reason == "venue_requested_reconnect" and result.reconnect_line is None:
            problems.append(
                f"segment {expected_index}: manifest says requested reconnect but payload does not"
            )
        if result.reconnect_line is not None and reason != "venue_requested_reconnect":
            problems.append(
                f"segment {expected_index}: payload requests reconnect but end_reason={reason!r}"
            )

        checkpoint_micro: Optional[int] = None
        checkpoint_metadata = segment.get("checkpoint_metadata")
        checkpoint_path = root / checkpoint_name
        if reason == "graceful_checkpoint" or checkpoint_metadata is not None:
            checkpoint_micro, checkpoint_problems = validate_snapshot_record(
                checkpoint_path,
                checkpoint_metadata,
                f"segment {expected_index} terminal checkpoint",
            )
            problems.extend(checkpoint_problems)
            if (
                snapshot_micro is not None
                and checkpoint_micro is not None
                and checkpoint_micro <= snapshot_micro
            ):
                problems.append(
                    f"segment {expected_index}: terminal checkpoint must be later than "
                    "the start snapshot"
                )

            if isinstance(checkpoint_metadata, dict):
                drain_seconds = checkpoint_metadata.get(
                    "post_checkpoint_drain_seconds"
                )
                if (
                    not isinstance(drain_seconds, (int, float))
                    or isinstance(drain_seconds, bool)
                    or drain_seconds <= 0
                ):
                    problems.append(
                        f"segment {expected_index}: invalid post-checkpoint drain duration"
                    )

                ordinal_fields = (
                    "capture_ordinal_before_request",
                    "capture_ordinal_at_receive",
                    "capture_ordinal_after_drain",
                )
                ordinals: list[int] = []
                for field_name in ordinal_fields:
                    value = checkpoint_metadata.get(field_name)
                    if (
                        not isinstance(value, int)
                        or isinstance(value, bool)
                        or value < 0
                    ):
                        problems.append(
                            f"segment {expected_index}: invalid checkpoint {field_name}"
                        )
                    else:
                        ordinals.append(value)
                if len(ordinals) == len(ordinal_fields) and ordinals != sorted(ordinals):
                    problems.append(
                        f"segment {expected_index}: checkpoint capture ordinals move backwards"
                    )
                if (
                    len(ordinals) == len(ordinal_fields)
                    and result.last_capture_ordinal is not None
                    and ordinals[-1] != result.last_capture_ordinal
                ):
                    problems.append(
                        f"segment {expected_index}: checkpoint drain ordinal does not "
                        "match the final frame"
                    )

                received_at = parse_manifest_time(
                    checkpoint_metadata.get("received_at"),
                    f"segment {expected_index} terminal checkpoint received_at",
                    problems,
                )
                drain_completed_at = parse_manifest_time(
                    checkpoint_metadata.get("drain_completed_at"),
                    f"segment {expected_index} terminal checkpoint drain_completed_at",
                    problems,
                )
                if (
                    received_at is not None
                    and drain_completed_at is not None
                    and drain_completed_at < received_at
                ):
                    problems.append(
                        f"segment {expected_index}: checkpoint drain completed before "
                        "the checkpoint was received"
                    )

        if reason == "graceful_checkpoint" and checkpoint_metadata is None:
            problems.append(
                f"segment {expected_index}: graceful stop has no terminal checkpoint"
            )

        print_segment(result, snapshot_micro, checkpoint_micro)
        problems.extend(
            f"segment {expected_index}: {problem}" for problem in result.problems
        )

    if manifest.get("lastCaptureOrdinal") != previous_capture_ordinal:
        problems.append(
            "manifest lastCaptureOrdinal does not match the final indexed captureOrdinal"
        )
    if segments and isinstance(segments[-1], dict):
        if segments[-1].get("end_reason") != "graceful_checkpoint":
            problems.append(
                "completed capture does not end with a graceful terminal checkpoint"
            )

    print(f"manifest        : {manifest_path}")
    print(f"status          : {manifest.get('status', '<missing>')}")
    print(f"segments        : {len(segments):,}")
    print(f"raw frames      : {total_frames:,}")
    print(f"order events    : {total_orders:,}")
    print(f"trade events    : {total_trades:,}")
    if problems:
        print(f"MANIFEST RESULT : INVALID ({len(problems)} problems)")
        for problem in problems[:30]:
            print(f"  {problem}")
        if len(problems) > 30:
            print(f"  ... and {len(problems) - 30} more")
        return 1
    print("MANIFEST RESULT : VALID")
    return 0
