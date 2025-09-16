#!/usr/bin/env python3
"""
Scan Unreal plugin Source/ directory and ensure every .h/.cpp file starts with a copyright header.
If missing, add it. If present, update year range to include current year.

This version:
- Strips any UTF-8 BOM (U+FEFF) that may exist at the start of the file.
- Reads with utf-8-sig and writes with utf-8 (no BOM) to prevent BOM from appearing before includes/pragma lines.
- Preserves existing line endings (LF or CRLF) when inserting/updating the header.
- Handles year ranges with hyphen or en dash.
"""

import os
import re
import datetime
import sys

HEADER_TEXT = "// Copyright (c) {years} Shocap Entertainment | Athomas Goldberg. All Rights Reserved."
CURRENT_YEAR = datetime.date.today().year

YEAR_RANGE_SPLIT_RE = re.compile(r"[–-]")  # hyphen or en dash


def detect_eol(lines):
    # Try to preserve existing newline style
    for l in lines:
        if l.endswith("\r\n"):
            return "\r\n"
        if l.endswith("\n"):
            return "\n"
    # Default to '\n' if we can't detect
    return "\n"


def normalize_years(years_str):
    years_str = years_str.strip()
    if YEAR_RANGE_SPLIT_RE.search(years_str):
        parts = YEAR_RANGE_SPLIT_RE.split(years_str, maxsplit=1)
        try:
            start_year = int(parts[0].strip())
        except Exception:
            start_year = CURRENT_YEAR
        return f"{start_year}-{CURRENT_YEAR}"
    else:
        try:
            y = int(years_str)
        except Exception:
            y = CURRENT_YEAR
        return f"{y}-{CURRENT_YEAR}" if y != CURRENT_YEAR else f"{CURRENT_YEAR}"


def process_file(path):
    # Read using utf-8-sig to automatically remove BOM if present
    with open(path, "r", encoding="utf-8-sig", errors="ignore", newline="") as f:
        lines = f.readlines()

    if not lines:
        return

    # Defensive: strip any lingering BOM at the very start of the file
    if lines and lines[0].startswith("\ufeff"):
        lines[0] = lines[0].lstrip("\ufeff")

    eol = detect_eol(lines)

    # Work with the first line without its trailing newline
    first_line_no_eol = lines[0].rstrip("\r\n")
    updated = False

    # Regex to detect an existing copyright header on the first line
    # Capture the year or year range and the holder text (unused, but tolerated)
    m = re.match(r"//\s*Copyright\s*\(c\)\s*([0-9]{4}(?:[–-][0-9]{4})?)\s+(.+)", first_line_no_eol, flags=re.I)
    if m:
        years_str = m.group(1)
        years = normalize_years(years_str)
        desired_header_line = f"{HEADER_TEXT.format(years=years)}"
        if first_line_no_eol != desired_header_line:
            lines[0] = desired_header_line + eol
            updated = True
    else:
        # Insert a new header at the top
        years = str(CURRENT_YEAR)
        new_header = f"{HEADER_TEXT.format(years=years)}{eol}"
        lines.insert(0, new_header)
        updated = True

    if updated:
        # Write as utf-8 (no BOM) and preserve exact newlines in 'lines'
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.writelines(lines)
        print(f"Updated: {path}")


def main():
    if len(sys.argv) < 2:
        print("Usage: add_copyright_headers.py <PluginRoot>")
        sys.exit(1)
    root = sys.argv[1]
    source_root = os.path.join(root, "Source")
    if not os.path.isdir(source_root):
        print(f"No Source folder under {root}")
        sys.exit(1)
    for dirpath, _, filenames in os.walk(source_root):
        for fn in filenames:
            if fn.endswith((".h", ".hpp", ".cpp", ".cxx")):
                process_file(os.path.join(dirpath, fn))


if __name__ == "__main__":
    main()
