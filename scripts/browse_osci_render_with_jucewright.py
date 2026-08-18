#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))
SHARED_TOOLS = SCRIPT_DIR.parent / "modules" / "osci_standalone" / "tools"
if str(SHARED_TOOLS) not in sys.path:
    sys.path.insert(0, str(SHARED_TOOLS))

from jucewright_osci_browser.cli import main


if __name__ == "__main__":
    sys.exit(main())
