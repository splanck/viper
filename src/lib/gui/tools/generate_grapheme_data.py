#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//
#
# File: lib/gui/tools/generate_grapheme_data.py
# Purpose: Generate dependency-free Unicode grapheme property tables and copy
#          the matching official conformance corpus into the GUI test data.
# Key invariants:
#   - All generated ranges are sorted, disjoint, and sourced from one pinned
#     Unicode release.
#   - Generated output is deterministic for identical Unicode source files.
# Ownership/Lifetime:
#   - Source bytes are held only for the duration of this process.
#   - Generated files are checked in and require no generator at build time.
# Links: lib/gui/src/core/vg_grapheme.c,
#        lib/gui/include/vg_grapheme.h,
#        https://www.unicode.org/reports/tr29/
#
#===----------------------------------------------------------------------===//

"""Generate ZannaGUI's Unicode 17 grapheme-break data.

The normal build never runs this script. Maintainers run it only when updating
the pinned Unicode version, then review and commit the deterministic outputs.
It uses Python's standard library and the Unicode Consortium's normative data.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import urllib.request
from dataclasses import dataclass
from typing import Iterable


UNICODE_VERSION = "17.0.0"
UNICODE_BASE = f"https://www.unicode.org/Public/{UNICODE_VERSION}/ucd"
SOURCE_PATHS = {
    "grapheme": "auxiliary/GraphemeBreakProperty.txt",
    "indic": "DerivedCoreProperties.txt",
    "emoji": "emoji/emoji-data.txt",
    "tests": "auxiliary/GraphemeBreakTest.txt",
}
GCB_VALUES = {
    "CR": "VG_GRAPHEME_GCB_CR",
    "LF": "VG_GRAPHEME_GCB_LF",
    "Control": "VG_GRAPHEME_GCB_CONTROL",
    "Extend": "VG_GRAPHEME_GCB_EXTEND",
    "Regional_Indicator": "VG_GRAPHEME_GCB_REGIONAL_INDICATOR",
    "Prepend": "VG_GRAPHEME_GCB_PREPEND",
    "SpacingMark": "VG_GRAPHEME_GCB_SPACING_MARK",
    "L": "VG_GRAPHEME_GCB_L",
    "V": "VG_GRAPHEME_GCB_V",
    "T": "VG_GRAPHEME_GCB_T",
    "LV": "VG_GRAPHEME_GCB_LV",
    "LVT": "VG_GRAPHEME_GCB_LVT",
    "ZWJ": "VG_GRAPHEME_GCB_ZWJ",
}
INCB_VALUES = {
    "Linker": "VG_GRAPHEME_INCB_LINKER",
    "Consonant": "VG_GRAPHEME_INCB_CONSONANT",
    "Extend": "VG_GRAPHEME_INCB_EXTEND",
}


@dataclass(frozen=True, order=True)
class Range:
    """One inclusive Unicode scalar range associated with a C enum value."""

    first: int
    last: int
    value: str


def parse_args() -> argparse.Namespace:
    """Parse generator inputs and return an immutable command-line namespace."""

    script_dir = pathlib.Path(__file__).resolve().parent
    gui_dir = script_dir.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ucd-dir",
        type=pathlib.Path,
        help="Read an unpacked Unicode UCD directory instead of downloading official files.",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=gui_dir / "src/core/vg_grapheme_data.inc",
        help="Generated C property-table include.",
    )
    parser.add_argument(
        "--test-output",
        type=pathlib.Path,
        default=gui_dir / f"tests/data/GraphemeBreakTest-{UNICODE_VERSION}.txt",
        help="Copied official conformance test data.",
    )
    return parser.parse_args()


def load_source(relative_path: str, ucd_dir: pathlib.Path | None) -> bytes:
    """Load one pinned UCD file from disk or the official HTTPS endpoint.

    Args:
        relative_path: Path relative to the root of the pinned UCD release.
        ucd_dir: Optional unpacked UCD root supplied by the maintainer.

    Returns:
        Exact source bytes used for parsing and digest generation.
    """

    if ucd_dir is not None:
        return (ucd_dir / relative_path).read_bytes()
    with urllib.request.urlopen(f"{UNICODE_BASE}/{relative_path}") as response:
        return response.read()


def parse_range(field: str) -> tuple[int, int]:
    """Parse a UCD scalar or inclusive scalar range into integer endpoints."""

    pieces = field.strip().split("..", maxsplit=1)
    first = int(pieces[0], 16)
    last = int(pieces[1], 16) if len(pieces) == 2 else first
    return first, last


def validate_ranges(ranges: list[Range], table_name: str) -> list[Range]:
    """Sort ranges and reject overlap, invalid scalars, or inverted endpoints."""

    ordered = sorted(ranges)
    previous_last = -1
    for item in ordered:
        if item.first < 0 or item.last > 0x10FFFF or item.first > item.last:
            raise ValueError(f"{table_name}: invalid range {item}")
        if item.first <= previous_last:
            raise ValueError(f"{table_name}: overlapping range {item}")
        previous_last = item.last
    return ordered


def parse_gcb(source: bytes) -> list[Range]:
    """Parse non-Other Grapheme_Cluster_Break ranges except algorithmic Hangul.

    UAX #29 derives LV/LVT directly from the Hangul syllable index. Omitting
    those 11,172 scalars keeps the generated binary-search table compact while
    preserving exactly the same property result in ``vg_grapheme.c``.
    """

    result: list[Range] = []
    pattern = re.compile(r"^([0-9A-F]+(?:\.\.[0-9A-F]+)?)\s*;\s*([A-Za-z_]+)")
    for line in source.decode("utf-8").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        value = GCB_VALUES.get(match.group(2))
        if value is None:
            raise ValueError(f"unknown Grapheme_Cluster_Break value: {match.group(2)}")
        first, last = parse_range(match.group(1))
        if first >= 0xAC00 and last <= 0xD7A3 and match.group(2) in {"LV", "LVT"}:
            continue
        result.append(Range(first, last, value))
    return validate_ranges(result, "Grapheme_Cluster_Break")


def parse_incb(source: bytes) -> list[Range]:
    """Parse all non-None Indic_Conjunct_Break ranges used by UAX #29 GB9c."""

    result: list[Range] = []
    pattern = re.compile(
        r"^([0-9A-F]+(?:\.\.[0-9A-F]+)?)\s*;\s*InCB\s*;\s*([A-Za-z_]+)"
    )
    for line in source.decode("utf-8").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        value = INCB_VALUES.get(match.group(2))
        if value is None:
            raise ValueError(f"unknown Indic_Conjunct_Break value: {match.group(2)}")
        first, last = parse_range(match.group(1))
        result.append(Range(first, last, value))
    return validate_ranges(result, "Indic_Conjunct_Break")


def parse_extended_pictographic(source: bytes) -> list[Range]:
    """Parse Extended_Pictographic=Yes ranges used by UAX #29 GB11."""

    result: list[Range] = []
    pattern = re.compile(
        r"^([0-9A-F]+(?:\.\.[0-9A-F]+)?)\s*;\s*Extended_Pictographic\b"
    )
    for line in source.decode("utf-8").splitlines():
        match = pattern.match(line)
        if match:
            first, last = parse_range(match.group(1))
            result.append(Range(first, last, "1"))
    return validate_ranges(result, "Extended_Pictographic")


def emit_ranges(name: str, ranges: Iterable[Range]) -> list[str]:
    """Render one sorted range list as a static C initializer."""

    rows = list(ranges)
    output = [f"static const vg_grapheme_property_range_t {name}[] = {{"]
    output.extend(
        f"    {{0x{item.first:06X}u, 0x{item.last:06X}u, {item.value}}}," for item in rows
    )
    output.append("};")
    output.append(f"static const size_t {name}_count = sizeof({name}) / sizeof({name}[0]);")
    return output


def render_data(
    gcb: list[Range], incb: list[Range], emoji: list[Range], digests: dict[str, str]
) -> str:
    """Render the complete generated C include with provenance and checksums."""

    lines = [
        "//===----------------------------------------------------------------------===//",
        "//",
        "// Part of the Zanna project, under the GNU GPL v3.",
        "// See LICENSE for license information.",
        "//",
        "//===----------------------------------------------------------------------===//",
        "//",
        "// File: lib/gui/src/core/vg_grapheme_data.inc",
        "// Purpose: Generated Unicode property ranges for extended grapheme segmentation.",
        "// Key invariants:",
        "//   - Ranges in each table are sorted, inclusive, and non-overlapping.",
        "//   - Hangul LV/LVT values are derived algorithmically and intentionally omitted.",
        f"//   - Property values implement Unicode {UNICODE_VERSION} / UAX #29 revision 47.",
        "// Ownership/Lifetime:",
        "//   - Tables have static storage and own no heap memory.",
        "// Links: lib/gui/tools/generate_grapheme_data.py,",
        "//        https://www.unicode.org/reports/tr29/tr29-47.html",
        "//",
        "// Unicode data copyright © Unicode, Inc.; used under",
        "// https://www.unicode.org/license.txt.",
        "//",
        f"// GraphemeBreakProperty.txt SHA-256: {digests['grapheme']}",
        f"// DerivedCoreProperties.txt SHA-256: {digests['indic']}",
        f"// emoji-data.txt SHA-256: {digests['emoji']}",
        "//",
        "// Generated file. Do not edit by hand.",
        "//",
        "//===----------------------------------------------------------------------===//",
        "",
    ]
    lines.extend(emit_ranges("g_grapheme_gcb_ranges", gcb))
    lines.append("")
    lines.extend(emit_ranges("g_grapheme_incb_ranges", incb))
    lines.append("")
    lines.extend(emit_ranges("g_grapheme_extended_pictographic_ranges", emoji))
    lines.append("")
    return "\n".join(lines)


def write_if_changed(path: pathlib.Path, data: bytes) -> None:
    """Write bytes atomically enough for this maintainer tool and avoid timestamp churn."""

    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main() -> None:
    """Load the pinned UCD inputs, validate them, and update generated outputs."""

    args = parse_args()
    sources = {
        key: load_source(relative, args.ucd_dir) for key, relative in SOURCE_PATHS.items()
    }
    digests = {key: hashlib.sha256(value).hexdigest() for key, value in sources.items()}
    generated = render_data(
        parse_gcb(sources["grapheme"]),
        parse_incb(sources["indic"]),
        parse_extended_pictographic(sources["emoji"]),
        digests,
    ).encode("utf-8")
    write_if_changed(args.output, generated)
    write_if_changed(args.test_output, sources["tests"])


if __name__ == "__main__":
    main()
