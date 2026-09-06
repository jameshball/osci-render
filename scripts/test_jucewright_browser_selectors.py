#!/usr/bin/env python3
"""Runner selector regressions; no application or Jucewright process required."""
import json
import tempfile
import unittest
from pathlib import Path

from jucewright_osci_browser.controls import ControlDiscoveryMixin
from jucewright_osci_browser.scenario import OsciRenderBrowserRun


def close_button(ref):
    return {"ref": ref, "role": "button", "name": "Close icon", "toggleable": True,
            "actions": ["click", "set_checked"]}


def dialog(ref, children):
    return {"ref": ref, "role": "dialogWindow", "children": children}


class BrowserSelectorTests(unittest.TestCase):
    def test_close_targets_topmost_dialog_not_underlying_dialog_or_popout(self):
        tree = {"children": [dialog("feedback", [close_button("feedback-close")]),
                             dialog("settings", [close_button("settings-close")]),
                             {"role": "window", "children": [close_button("popout-close")]}]}
        self.assertEqual(OsciRenderBrowserRun.overlay_close_ref(tree), "settings-close")

    def test_animation_does_not_close_underlying_dialog(self):
        tree = {"children": [dialog("feedback", [close_button("feedback-close")]),
                             dialog("settings", [{"class": "AnimationImageComponent"}])]}
        self.assertIsNone(OsciRenderBrowserRun.overlay_close_ref(tree))

    def test_hidden_dialog_is_not_selected(self):
        hidden = dialog("hidden", [close_button("hidden-close")])
        hidden["visible"] = False
        tree = {"children": [dialog("visible", [close_button("visible-close")]), hidden]}
        self.assertEqual(OsciRenderBrowserRun.overlay_close_ref(tree), "visible-close")

    def test_sweep_preserves_dialog_and_exercises_real_toggle(self):
        tree = {"children": [close_button("close"),
                             {"class": "osci::CloseButton", "children": [
                                 {"ref": "unnamed-close", "role": "button", "toggleable": True}]},
                             {"ref": "toggle", "role": "button", "name": "Diagnostic log",
                              "toggleable": True, "checked": False}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "snapshot.json"
            path.write_text(json.dumps({"tree": tree}))
            rows = ControlDiscoveryMixin().discover_visible_controls(path, 0)
        self.assertEqual(rows, [("toggle", "button", "set-checked", "true", "Diagnostic log")])

    def test_composite_remove_click_targets_inner_button(self):
        run = object.__new__(OsciRenderBrowserRun)
        commands = []
        run.cli = lambda *args: list(args)

        def call(command):
            commands.append(command)
            return json.dumps({"tree": {"ref": "container", "role": "unspecified", "children": [
                {"ref": "remove-icon", "role": "button", "name": "Remove image icon"}]}})

        run.call = call
        run.click_component_button("removeUserScreenshot1")
        self.assertEqual(commands[1][:2], ["click", "remove-icon"])
        self.assertIn("removeUserScreenshot1", commands[0])

    def test_switch_waits_for_actionability_before_snapshot(self):
        run = object.__new__(OsciRenderBrowserRun)
        commands = []
        run.cli = lambda *args: list(args)
        run.call = lambda command: commands.append(command)

        def snapshot(command, path, stderr):
            commands.append(command)
            path.write_text(json.dumps({"tree": {"ref": "toggle", "checked": True,
                                                 "actions": ["set_checked"]}}))
            return True

        run.call_to_file = snapshot
        with tempfile.TemporaryDirectory() as directory:
            run.artifact_dir = Path(directory)
            self.assertEqual(run.checked_switch_ref_for_component("diagnostic_log", by_id=True), ("toggle", True))
        self.assertEqual(commands[0][0], "click")
        self.assertIn("--trial", commands[0])
        self.assertEqual(commands[1][0], "snapshot")
        for command in commands:
            self.assertIn("--component-id", command)
            self.assertIn("diagnostic_log", command)


if __name__ == "__main__":
    unittest.main()
