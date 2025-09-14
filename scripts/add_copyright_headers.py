#!/usr/bin/env python3
"""
Scan Unreal plugin Source/ directory and ensure every .h/.cpp file starts with a copyright header.
If missing, add it. If present, update year range to include current year.
"""

import os, re, datetime, sys

HEADER_TEMPLATE = "// Copyright (c) {years} Shocap Entertainment / Athomas Goldberg. All Rights Reserved.\n"
CURRENT_YEAR = datetime.date.today().year

def process_file(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
    if not lines:
        return
    header_line = lines[0].strip()
    updated = False

    # Regex to detect existing copyright header
    m = re.match(r"//\s*Copyright\s*\(c\)\s*([0-9\-–]+)\s+(.*)", header_line, flags=re.I)
    if m:
        years_str = m.group(1)
        holder = m.group(2)
        # Normalize years
        if "-" in years_str or "–" in years_str:
            start, _, end = years_str.partition("-")
            try:
                start_year = int(start.strip())
            except:
                start_year = CURRENT_YEAR
            years = f"{start_year}-{CURRENT_YEAR}"
        else:
            try:
                y = int(years_str.strip())
            except:
                y = CURRENT_YEAR
            if y != CURRENT_YEAR:
                years = f"{y}-{CURRENT_YEAR}"
            else:
                years = f"{CURRENT_YEAR}"
        new_header = HEADER_TEMPLATE.format(years=years)
        if header_line + "\n" != new_header:
            lines[0] = new_header
            updated = True
    else:
        # Insert new header at top
        years = str(CURRENT_YEAR)
        new_header = HEADER_TEMPLATE.format(years=years)
        lines.insert(0, new_header)
        updated = True

    if updated:
        with open(path, "w", encoding="utf-8") as f:
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
            if fn.endswith((".h",".hpp",".cpp",".cxx")):
                process_file(os.path.join(dirpath, fn))

if __name__ == "__main__":
    main()
