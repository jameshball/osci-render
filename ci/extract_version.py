#!/usr/bin/env python3
"""Extract the version attribute from a JUCE .jucer project file.

Usage:
  python3 ci/extract_version.py path/to/project.jucer

Prints the version to stdout and exits 0 on success.
Returns exit code 2 if version attribute is missing or empty.
Returns exit code 3 on parse error / unreadable file.
"""
from __future__ import annotations
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: extract_version.py <project.jucer>", file=sys.stderr)
        return 1
    path = Path(sys.argv[1])
    if not path.is_file():
        print(f"File not found: {path}", file=sys.stderr)
        return 1
    try:
        root = ET.parse(path).getroot()
    except Exception as e:  # noqa: BLE001
        print(f"Parse error: {e}", file=sys.stderr)
        return 3
    version = (root.attrib.get("version") or "").strip()
    if not version:
        print("Missing version attribute", file=sys.stderr)
        return 2
    print(version)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
