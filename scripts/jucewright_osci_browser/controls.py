from __future__ import annotations

import json
import os
from pathlib import Path

from .constants import SKIP_CONTROL_NAMES
from jucewright_browser.errors import StepError
from .png import check_png_not_blank
from jucewright_browser.utils import numeric, slug, walk_tree


class ControlDiscoveryMixin:
    PROTECTED_GENERIC_CONTROL_CLASSES = {
        "MidiComponent",
        "juce::CustomMidiKeyboardComponent",
        "CustomMidiKeyboardComponent",
    }

    PROTECTED_GENERIC_COMPONENT_NAMES = {
        "midi",
        "inputenabled",
    }

    CONSERVATIVE_VISUALISER_SLIDERS = {
        "line intensity",
        "persistence",
        "focus",
        "glow",
        "afterglow",
        "overexposure",
        "noise",
        "ambient light",
    }

    def check_png_not_blank(self, file, **kwargs):
        return check_png_not_blank(file, **kwargs)

    def check_visualiser_png_not_blank(self, file):
        return check_png_not_blank(file, crop_bottom_fraction=0.12)

    def node_label(self, node: dict) -> str:
        for key in ["name", "title", "componentName", "componentId", "class"]:
            value = str(node.get(key, "")).strip()
            if value:
                return value
        return str(node.get("ref", "unnamed"))

    def is_visible_control(self, node: dict) -> bool:
        if not node.get("visible", True) or not node.get("enabled", True):
            return False
        if node.get("role") == "ignored" or not node.get("ref"):
            return False
        if SKIP_CONTROL_NAMES.search(self.node_label(node)):
            return False
        bounds = node.get("bounds", {})
        return bounds.get("w", 1) > 0 and bounds.get("h", 1) > 0

    def should_skip_control_path(self, path: tuple[dict, ...]) -> bool:
        for node in path:
            if str(node.get("class", "")) in self.PROTECTED_GENERIC_CONTROL_CLASSES:
                return True

            for key in ["name", "componentName", "componentId"]:
                value = str(node.get(key, "")).strip().lower()
                if value in self.PROTECTED_GENERIC_COMPONENT_NAMES:
                    return True

        return False

    def conservative_visualiser_slider_value(self, node: dict, label: str, minimum: float, maximum: float) -> float | None:
        if label.strip().lower() not in self.CONSERVATIVE_VISUALISER_SLIDERS:
            return None

        current = numeric(node.get("value"))
        span = maximum - minimum

        if current is None:
            return minimum + span * 0.25

        if label.strip().lower() in {"line intensity", "focus"}:
            return max(minimum, current - span * 0.05)

        return min(maximum, current + span * 0.05)

    def slider_value(self, node: dict, index: int) -> float | None:
        minimum = numeric(node.get("minimum"))
        maximum = numeric(node.get("maximum"))
        current = numeric(node.get("value"))
        if minimum is None or maximum is None or maximum <= minimum:
            if current is None:
                return None
            return current + (0.1 if index % 2 == 0 else -0.1)

        label = self.node_label(node)
        conservative_value = self.conservative_visualiser_slider_value(node, label, minimum, maximum)
        if conservative_value is not None:
            value = conservative_value
        else:
            fraction = 0.35 if index % 2 == 0 else 0.65
            value = minimum + (maximum - minimum) * fraction

        interval = numeric(node.get("interval"))
        if interval is not None and interval > 0:
            value = round(value / interval) * interval
        return max(minimum, min(maximum, value))

    def first_enabled_option_index(self, node: dict):
        options = node.get("options", [])
        if not isinstance(options, list):
            return None

        current_text = str(node.get("value", ""))
        fallback = None
        for option in options:
            if not isinstance(option, dict):
                continue
            if not option.get("enabled", True) or option.get("separator", False) or option.get("sectionHeader", False):
                continue
            if option.get("hasSubMenu", False):
                continue
            index = option.get("index")
            if index is None:
                continue
            if fallback is None:
                fallback = index
            if current_text and str(option.get("text", "")) != current_text:
                return index
        return fallback

    def walk_tree_with_path(self, node: dict, path: tuple[dict, ...] = ()):
        current_path = (*path, node)
        yield node, current_path
        for child in node.get("children", []) or []:
            if isinstance(child, dict):
                yield from self.walk_tree_with_path(child, current_path)

    def discover_visible_controls(self, snapshot_file: Path, max_controls: int) -> list[tuple[str, str, str, str, str]]:
        data = json.loads(snapshot_file.read_text(encoding="utf-8"))
        root = data.get("tree", {})
        rows: list[tuple[str, str, str, str, str]] = []

        for node, path in self.walk_tree_with_path(root):
            if max_controls and len(rows) >= max_controls:
                break
            if self.should_skip_control_path(path):
                continue
            if not self.is_visible_control(node):
                continue

            role = node.get("role", "")
            actions = node.get("actions", []) or []
            name = self.node_label(node).replace("\t", " ").replace("\n", " ")
            ref = node["ref"]

            if role == "slider":
                value = self.slider_value(node, len(rows))
                if value is not None:
                    rows.append((ref, role, "set-value", f"{value:.8g}", name))
            elif role == "comboBox":
                index = self.first_enabled_option_index(node)
                if index is not None:
                    rows.append((ref, role, "select-index", str(index), name))
            elif role in ["button", "toggleButton", "checkBox"] and node.get("toggleable", False):
                desired = "false" if bool(node.get("checked", node.get("toggleState", False))) else "true"
                rows.append((ref, role, "set-checked", desired, name))
            elif role == "editableText" and not node.get("readOnly", False):
                rows.append((ref, role, "fill", "automation", name))
            elif "set_checked" in actions:
                desired = "false" if bool(node.get("checked", False)) else "true"
                rows.append((ref, role, "set-checked", desired, name))

        return rows

    def exercise_visible_controls(self, label: str, max_controls: int, *locator_args: object) -> None:
        self.step += 1
        snapshot_file = self.artifact_dir / f"{self.step:03d}_dynamic_{slug(label)}_snapshot.json"
        controls_file = self.artifact_dir / f"{self.step:03d}_dynamic_{slug(label)}_controls.tsv"

        self.log(f"DISCOVER visible controls in {label}")
        if not self.call_to_file(self.cli("snapshot", "--json", "--full", "--depth", "18", *locator_args), snapshot_file, snapshot_file.with_suffix(snapshot_file.suffix + ".stderr")):
            self.log(f"SKIP/FAIL optional dynamic snapshot {label} -> {snapshot_file}.stderr")
            self.optional_failures.append(f"dynamic snapshot {label} (exit 1): {snapshot_file}.stderr")
            return

        try:
            rows = self.discover_visible_controls(snapshot_file, max_controls)
            controls_file.write_text("".join("\t".join(row) + "\n" for row in rows), encoding="utf-8")
        except Exception as exc:
            controls_file.with_suffix(controls_file.suffix + ".stderr").write_text(str(exc) + "\n", encoding="utf-8")
            self.log(f"SKIP/FAIL optional dynamic discovery {label} -> {controls_file}.stderr")
            self.optional_failures.append(f"dynamic discovery {label} (exit 1): {controls_file}.stderr")
            return

        name_filter = os.environ.get("DYNAMIC_NAME_FILTER", "").lower()
        if name_filter:
            rows = [row for row in rows if name_filter in row[4].lower()]

        if not rows:
            self.log(f"INFO no visible controls discovered for {label} -> {controls_file}")
            return

        for ref, _role, action, value, name in rows:
            if action == "set-value":
                self.try_step(f"dynamic {label} set slider {name}", self.cli("set-value", ref, "--timeout-ms", "3000", value))
            elif action == "select-index":
                self.try_step(f"dynamic {label} select option {name}", self.cli("select-option", ref, "--index", value, "--timeout-ms", "3000"))
            elif action == "set-checked":
                self.try_step(f"dynamic {label} set checked {name}", self.cli("set-checked", ref, "--timeout-ms", "3000", value))
            elif action == "fill":
                self.try_step(f"dynamic {label} fill {name}", self.cli("fill", ref, "--timeout-ms", "3000", value))

    def verify_new_recording_file(self, marker_file: Path) -> None:
        marker_time = marker_file.stat().st_mtime
        extensions = {".mp4", ".mov", ".wav", ".m4v"}
        candidates: list[tuple[float, int, Path]] = []

        for root, _dirs, files in os.walk(self.recording_dir):
            for name in files:
                path = Path(root) / name
                if path.suffix.lower() not in extensions:
                    continue
                stat = path.stat()
                if stat.st_mtime >= marker_time and stat.st_size > 512:
                    candidates.append((stat.st_mtime, stat.st_size, path))

        if not candidates:
            raise StepError(f"no recording file larger than 512 bytes was created in {self.recording_dir}")

        for _mtime, size, path in sorted(candidates, reverse=True)[:3]:
            print(f"{path}\t{size} bytes")

    def component_size(self, label: str, *locator_args: object) -> tuple[int, int]:
        snapshot_file = self.artifact_dir / f"geometry_{slug(label)}.json"
        if not self.call_to_file(self.cli("snapshot", "--json", "--interesting", "--depth", "4", *locator_args), snapshot_file, snapshot_file.with_suffix(snapshot_file.suffix + ".stderr")):
            raise StepError(f"geometry snapshot failed for {label}")

        data = json.loads(snapshot_file.read_text(encoding="utf-8"))
        for node in walk_tree(data.get("tree", {})):
            bounds = node.get("bounds", {})
            width = int(round(float(bounds.get("w", 0))))
            height = int(round(float(bounds.get("h", 0))))
            if width > 0 and height > 0:
                return width, height

        raise StepError("component has no usable bounds")

    def drag_component_percent(self, label: str, x_percent: int, y_percent: int, dx: int, dy: int, *locator_args: object) -> None:
        try:
            width, height = self.component_size(label, *locator_args)
        except Exception as exc:
            self.optional_failure(f"{label} geometry unavailable", str(exc))
            return

        x = max(1, min(width - 1, width * x_percent // 100))
        y = max(1, min(height - 1, height * y_percent // 100))
