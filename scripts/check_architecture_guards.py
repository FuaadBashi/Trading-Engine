#!/usr/bin/env python3
"""Enforce small architectural rules that the C++ compiler cannot express.

This is intentionally a source-policy check, not a market-data validator. It prevents production
code from bypassing the injected Clock seam and protects exact financial state from accidental
floating-point representation. Comments and string literals are masked before matching so examples
and documentation do not create false failures.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


CPP_SUFFIXES = {".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
PRODUCTION_ROOTS = ("include", "src", "apps", "strategies")
ALLOWED_CLOCK_FILES = {Path("src/core/time.cpp")}

# These files own exact prices, quantities, order/trade financial fields, book aggregates, or
# portfolio/risk state. Strategy/research/telemetry code is deliberately not banned wholesale:
# probabilities, statistics and graph coordinates may legitimately use floating point.
#
# Implementation files are listed alongside their headers. Declaring the type in an int64 header
# does not stop the arithmetic in the matching .cpp from passing through a double, which is
# exactly where a rounding error would enter. The two engine .cpp entries are listed before those
# files exist: the set is matched against files actually walked, so an entry for a missing file is
# inert until the file appears, and this way the guard is already in place the day it is written
# rather than being retrofitted afterwards (TODO C7).
EXACT_FINANCIAL_PATHS = {
    Path("include/te/core/types.hpp"),
    Path("include/te/feed/events.hpp"),
    Path("include/te/feed/trade_event.hpp"),
    Path("include/te/engine/portfolio.hpp"),
    Path("include/te/engine/risk.hpp"),
    Path("src/engine/portfolio.cpp"),
    Path("src/engine/risk.cpp"),
}
EXACT_FINANCIAL_DIRECTORIES = (Path("include/te/book"),)

CLOCK_CALL = re.compile(
    r"\bstd\s*::\s*chrono\s*::\s*(?:system_clock|steady_clock)\s*::\s*now\s*\("
)
FLOATING_TYPE = re.compile(r"\b(?:float|double)\b")


@dataclass(frozen=True)
class Violation:
    path: Path
    line: int
    message: str


def maskNonCode(text: str) -> str:
    """Replace C++ comments and literals with spaces while preserving newlines."""

    output = list(text)
    index = 0
    state = "code"
    rawEnd = ""

    def mask(position: int) -> None:
        if output[position] != "\n":
            output[position] = " "

    while index < len(text):
        if state == "line_comment":
            if text[index] == "\n":
                state = "code"
            else:
                mask(index)
            index += 1
            continue

        if state == "block_comment":
            if text.startswith("*/", index):
                mask(index)
                mask(index + 1)
                index += 2
                state = "code"
            else:
                mask(index)
                index += 1
            continue

        if state in {"string", "character"}:
            delimiter = '"' if state == "string" else "'"
            if text[index] == "\\" and index + 1 < len(text):
                mask(index)
                mask(index + 1)
                index += 2
            elif text[index] == delimiter:
                mask(index)
                index += 1
                state = "code"
            else:
                mask(index)
                index += 1
            continue

        if state == "raw_string":
            if text.startswith(rawEnd, index):
                for position in range(index, index + len(rawEnd)):
                    mask(position)
                index += len(rawEnd)
                state = "code"
            else:
                mask(index)
                index += 1
            continue

        if text.startswith("//", index):
            mask(index)
            mask(index + 1)
            index += 2
            state = "line_comment"
        elif text.startswith("/*", index):
            mask(index)
            mask(index + 1)
            index += 2
            state = "block_comment"
        elif text.startswith('R"', index):
            delimiterEnd = text.find("(", index + 2, index + 19)
            if delimiterEnd == -1:
                mask(index)
                index += 1
                state = "string"
            else:
                delimiter = text[index + 2 : delimiterEnd]
                rawEnd = ")" + delimiter + '"'
                for position in range(index, delimiterEnd + 1):
                    mask(position)
                index = delimiterEnd + 1
                state = "raw_string"
        elif text[index] == '"':
            mask(index)
            index += 1
            state = "string"
        elif text[index] == "'":
            mask(index)
            index += 1
            state = "character"
        else:
            index += 1

    return "".join(output)


def lineNumber(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def isExactFinancialPath(relativePath: Path) -> bool:
    if relativePath in EXACT_FINANCIAL_PATHS:
        return True
    return any(directory in relativePath.parents for directory in EXACT_FINANCIAL_DIRECTORIES)


def scanRepository(root: Path) -> list[Violation]:
    violations: list[Violation] = []

    for rootName in PRODUCTION_ROOTS:
        sourceRoot = root / rootName
        if not sourceRoot.exists():
            continue

        for path in sorted(sourceRoot.rglob("*")):
            if not path.is_file() or path.suffix not in CPP_SUFFIXES:
                continue

            relativePath = path.relative_to(root)
            source = path.read_text(encoding="utf-8")
            code = maskNonCode(source)

            if relativePath not in ALLOWED_CLOCK_FILES:
                for match in CLOCK_CALL.finditer(code):
                    violations.append(
                        Violation(
                            relativePath,
                            lineNumber(code, match.start()),
                            "direct operating-system clock access bypasses the injected Clock; "
                            "only src/core/time.cpp may create the real clock",
                        )
                    )

            if isExactFinancialPath(relativePath):
                for match in FLOATING_TYPE.finditer(code):
                    violations.append(
                        Violation(
                            relativePath,
                            lineNumber(code, match.start()),
                            "floating-point type in exact financial state; use the project's "
                            "integer strong types and instrument scale",
                        )
                    )

    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "root",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (defaults to the parent of scripts/)",
    )
    args = parser.parse_args()

    violations = scanRepository(args.root.resolve())
    for violation in violations:
        print(f"{violation.path}:{violation.line}: {violation.message}")

    if violations:
        print(f"architecture guards failed: {len(violations)} violation(s)")
        return 1

    print("architecture guards passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
