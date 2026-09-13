from __future__ import annotations

import math
import re


def slug(text: str) -> str:
    value = re.sub(r"[^A-Za-z0-9]+", "_", text.lower()).strip("_")
    return value or "step"


def bool_text(value: bool) -> str:
    return "true" if value else "false"


def numeric(value: object) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def walk_tree(node: dict):
    yield node
    for child in node.get("children", []) or []:
        if isinstance(child, dict):
            yield from walk_tree(child)
