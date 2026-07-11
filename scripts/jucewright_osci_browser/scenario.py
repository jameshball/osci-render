from __future__ import annotations

import json
import os
import platform
import shutil
from pathlib import Path

from .constants import (
    EFFECT_INDEX,
    EFFECTS,
    EXAMPLE_INDEX,
    FRACTAL_EXAMPLES,
    LUA_EXAMPLES,
    MODEL_EXAMPLES,
    MOD_HANDLE_INDEX,
    MOD_TABS,
    SVG_EXAMPLES,
    TEXT_EXAMPLES,
)
from .controls import ControlDiscoveryMixin
from .errors import StepError
from .feedback_mock import FeedbackMockServer
from .platform_support import is_executable
from .session import BrowserSession
from .utils import bool_text, slug, walk_tree


class OsciRenderBrowserRun(ControlDiscoveryMixin, BrowserSession):
    def exercise_feedback_dialog(self) -> None:
        self.run_step("open feedback dialog", self.select_menu_item("Send Feedback..."))
        self.run_step("feedback dialog snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "16", "--class", "osci::FeedbackOverlay"))
        self.run_step("feedback dialog screenshot", self.cli("screenshot", "--class", "osci::FeedbackOverlay", "--source", "auto", "--file", self.artifact_dir / "feedback-form.png"))
        for component_name, label in [
            ("App screenshot", "automatic screenshot enabled"),
            ("Diagnostic log", "diagnostic log enabled"),
            ("Current project", "project snapshot enabled"),
            ("Technical details", "technical details enabled"),
        ]:
            self.ensure_checked_switch(component_name, label, True)
        self.run_step("fill feedback email", self.cli("fill", "--component-id", "contact_email", "--timeout-ms", "3000", "automation@example.com"))
        self.run_step("fill feedback title", self.cli("fill", "--component-id", "feedback_title", "--timeout-ms", "3000", "Automation feedback report"))
        self.run_step("fill feedback details", self.cli("fill", "--component-id", "feedback_details", "--timeout-ms", "3000", "Jucewright verifies the complete in-app feedback submission flow."))
        self.run_step("drop user feedback screenshot", self.cli("drop-files", "--file", self.root_dir / "Resources" / "oscilloscope" / "real.png", "--class", "osci::FileDropZoneComponent", "--timeout-ms", "5000"))
        self.run_step("scroll feedback form to submit", self.cli("wheel", "550", "650", "--dy", "-8"))
        self.run_step("feedback footer screenshot", self.cli("screenshot", "--class", "osci::FeedbackOverlay", "--source", "auto", "--file", self.artifact_dir / "feedback-footer.png"))
        self.run_step("submit feedback", self.cli("click", "--component-id", "submitFeedback", "--timeout-ms", "5000"))
        self.run_step("wait for feedback reference", self.cli("wait-for-locator", "--text", "FB-AUTOMATION", "--timeout-ms", "20000"))
        self.run_step("wait for feedback success layout", self.cli("wait", "--ms", "500"))
        self.run_step("feedback success screenshot", self.cli("screenshot", "--class", "osci::FeedbackOverlay", "--source", "auto", "--file", self.artifact_dir / "feedback-success.png"))
        self.run_step("feedback mock payload validation", self.feedback_mock.assert_valid_submission)
        self.run_step("close feedback success", self.cli("click", "--component-id", "submitFeedback", "--timeout-ms", "3000"))
        self.run_step("open about dialog for feedback entry", self.select_menu_item("About osci-render"))
        self.run_step("open feedback from about dialog", self.cli("click", "--name", "Send Feedback", "--exact", "--timeout-ms", "5000"))
        self.run_step("wait for feedback from about dialog", self.cli("wait-for-locator", "--class", "osci::FeedbackOverlay", "--timeout-ms", "5000"))
        self.run_step("feedback from about screenshot", self.cli("screenshot", "--class", "osci::FeedbackOverlay", "--source", "auto", "--file", self.artifact_dir / "feedback-from-about.png"))
        self.run_step("close feedback from about dialog", self.close_overlay)

    def open_examples_panel(self) -> None:
        self.ensure_midi_mode(False, "before opening examples")
        self.try_step("disable external audio before opening examples", self.cli("set-checked", "--component-name", "inputEnabled", "--exact", "--timeout-ms", "3000", "false"))
        self.try_step("wait after disabling external audio before examples", self.cli("wait", "--ms", "250"))
        self.run_step("open files and examples panel", self.cli("click", "--name", "openFiles", "--exact", "--timeout-ms", "5000"))
        self.run_step("examples panel is open", self.cli("locator", "--class", "OpenFileComponent", "--timeout-ms", "5000"))
        self.try_step("examples panel snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "10"))

    def load_example(self, name: str, expect_source: bool = False) -> None:
        nth = EXAMPLE_INDEX.get(name)
        if nth is None:
            self.die(f"No example index mapping for '{name}'")

        self.open_examples_panel()
        self.run_step(f"load example {name}", self.cli("click", "--class", "osci::GridItemComponent", "--nth", nth, "--force", "--timeout-ms", "6000"))
        self.run_step(f"wait after loading {name}", self.cli("wait", "--ms", "400"))
        self.run_step(f"snapshot after loading {name}", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        if expect_source:
            self.try_step(f"source editor for {name}", self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "ErrorCodeEditorComponent", "--nth", "0"))
        self.try_step(f"visualiser screenshot after {name}", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / f"visualiser_{slug(name)}.png"))
        self.try_step(f"dismiss transient overlays after {name}", self.cli("press", "Escape"))

    def exercise_current_file_common(self, label: str) -> None:
        self.try_step(f"describe file controls for {label}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "FileControlsComponent"))
        self.try_step(f"describe quick controls for {label}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "QuickControlsBar"))
        self.try_step(f"randomise effects for {label}", self.cli("click", "--name", "randomise", "--exact", "--timeout-ms", "5000"))
        self.try_step(f"toggle auto-link lfos for {label}", self.cli("click", "--name", "autoLink", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"restore auto-link lfos for {label}", self.cli("click", "--name", "autoLink", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"visualiser screenshot for {label}", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / f"visualiser_common_{slug(label)}.png"))
        self.exercise_visible_controls(f"quick controls for {label}", 0, "--class", "QuickControlsBar")

    def exercise_text_file_controls(self, name: str) -> None:
        self.try_step(f"text font describe for {name}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "TxtComponent", "--nth", "0"))
        self.try_step(f"select text font for {name}", self.cli("select-option", "--class", "TxtComponent", "--nth", "0", "--index", "0", "--timeout-ms", "3000"))

    def exercise_lua_file_controls(self, name: str) -> None:
        self.try_step(f"lua source editor for {name}", self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "ErrorCodeEditorComponent", "--nth", "0"))
        self.try_step(f"reset lua state for {name}", self.cli("click", "--name", "luaReset", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"open lua scripting reference for {name}", self.cli("click", "--name", "luaHelp", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"lua scripting reference snapshot for {name}", self.cli("snapshot", "--json", "--interesting", "--depth", "10"))
        self.try_step(f"close lua scripting reference for {name}", self.cli("click", "--name", "closeOverlay", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"lua slider a for {name}", self.cli("set-value", "--role", "slider", "--name", "Lua Slider A", "--exact", "--timeout-ms", "3000", "0.25"))
        self.try_step(f"lua slider b for {name}", self.cli("set-value", "--role", "slider", "--name", "Lua Slider B", "--exact", "--timeout-ms", "3000", "0.75"))
        self.try_step(f"pause lua console for {name}", self.cli("click", "--name", "pauseConsole", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"clear lua console for {name}", self.cli("click", "--name", "clearConsole", "--exact", "--timeout-ms", "3000"))

    def exercise_fractal_file_controls(self, name: str) -> None:
        self.try_step(f"fractal editor describe for {name}", self.cli("describe", "--json", "--interesting", "--depth", "10", "--class", "FractalComponent"))
        self.exercise_visible_controls(f"fractal editor for {name}", 0, "--class", "FractalComponent")
        self.try_step(f"add fractal rule for {name}", self.cli("click", "--name", "+ Add Rule", "--exact", "--timeout-ms", "3000"))

    def exercise_frame_and_timeline_controls(self, label: str, kind: str) -> None:
        if kind in ["gpla", "gif", "mp4", "mov"]:
            self.try_step(f"frame settings describe for {label}", self.cli("describe", "--json", "--interesting", "--depth", "10", "--name", "Frame settings", "--exact"))
            self.try_step(f"set frames per second for {label}", self.cli("fill", "--role", "editableText", "--name", "Frames per second", "--exact", "--timeout-ms", "3000", "12.00"))

        if kind in ["gif", "png", "jpg", "mp4", "mov"]:
            self.try_step(f"toggle invert image for {label}", self.cli("click", "--role", "button", "--name", "Invert Image", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"set image threshold for {label}", self.cli("set-value", "--role", "slider", "--name", "Image Threshold", "--exact", "--timeout-ms", "3000", "0.4"))
            self.try_step(f"set image stride for {label}", self.cli("set-value", "--role", "slider", "--name", "Image Stride", "--exact", "--timeout-ms", "3000", "3"))

        if kind in ["gpla", "gif", "flac", "mp4", "mov"]:
            self.try_step(f"timeline describe for {label}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "TimelineComponent"))
            self.try_step(f"timeline pause for {label}", self.cli("click", "--name", "Pause", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"timeline play for {label}", self.cli("click", "--name", "Play", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"timeline repeat for {label}", self.cli("click", "--name", "Repeat", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"timeline pause after play for {label}", self.cli("click", "--name", "Pause", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"timeline stop for {label}", self.cli("click", "--name", "Stop", "--exact", "--timeout-ms", "3000"))

    def exercise_file_switching(self) -> None:
        self.try_step("switch to previous file", self.cli("click", "--name", "leftArrow", "--exact", "--timeout-ms", "3000"))
        self.try_step("snapshot after previous file", self.cli("snapshot", "--json", "--interesting", "--depth", "10"))
        self.try_step("switch to next file", self.cli("click", "--name", "rightArrow", "--exact", "--timeout-ms", "3000"))
        self.try_step("snapshot after next file", self.cli("snapshot", "--json", "--interesting", "--depth", "10"))

    def exercise_midi_keyboard_clicks(self) -> None:
        self.run_step("midi keyboard snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--timeout-ms", "5000"))
        self.run_step("midi keyboard low note click", self.cli("click", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--position", "16,24", "--force", "--timeout-ms", "3000"))
        self.run_step("midi keyboard middle note click", self.cli("click", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--position", "72,24", "--force", "--timeout-ms", "3000"))
        self.run_step("midi keyboard high note click", self.cli("click", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--position", "140,24", "--force", "--timeout-ms", "3000"))
        self.run_step("midi keyboard multi-note drag", self.cli("drag", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--position", "32,24", "--dx", "180", "--dy", "0", "--steps", "14", "--force", "--timeout-ms", "5000"))
        self.try_step("visualiser screenshot after midi key clicks", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / "visualiser_after_midi_keyboard.png"))

    def select_menu_item(self, item: str) -> list[str]:
        return self.cli("select-option", "--role", "menuBar", "--class", "MenuBarComponent", "--text", item, "--timeout-ms", "5000")

    def ensure_midi_keyboard_visible(self) -> None:
        file = self.next_step_file("midi keyboard visible", "probe")
        self.log("PROBE MIDI keyboard visible")
        if self.call_to_file(self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--timeout-ms", "2000"), file):
            self.log(f"OK  MIDI keyboard already visible -> {file}")
            return

        self.log(f"INFO MIDI keyboard hidden; enabling Interface > Show MIDI Keyboard -> {file}")
        self.run_step("show midi keyboard preference", self.select_menu_item("Show MIDI Keyboard"))
        self.run_step("wait after showing midi keyboard preference", self.cli("wait", "--ms", "500"))

    def checked_switch_ref_for_component(self, component_name: str) -> tuple[str, bool]:
        snapshot_file = self.artifact_dir / f"switch_{slug(component_name)}.json"
        if not self.call_to_file(self.cli("snapshot", "--json", "--interesting", "--depth", "6", "--component-name", component_name, "--timeout-ms", "3000"), snapshot_file, snapshot_file.with_suffix(snapshot_file.suffix + ".stderr")):
            raise StepError(f"could not snapshot switch component {component_name}")

        data = json.loads(snapshot_file.read_text(encoding="utf-8"))
        for node in walk_tree(data.get("tree", {})):
            actions = node.get("actions", []) or []
            if "set_checked" in actions:
                return str(node.get("ref", "")), bool(node.get("checked"))

        raise StepError("no checked switch descendant")

    def ensure_checked_switch(self, component_name: str, label: str, desired: bool = True) -> None:
        try:
            ref, checked = self.checked_switch_ref_for_component(component_name)
        except Exception:
            self.run_step(f"{label} switch lookup", lambda: (_ for _ in ()).throw(StepError("switch lookup failed")))
            return

        if checked == desired:
            self.try_step(f"{label} already {bool_text(desired)}", self.cli("wait", "--ms", "1"))
            return

        self.run_step(label, self.cli("set-checked", ref, "--timeout-ms", "3000", bool_text(desired)))

    def ensure_midi_mode(self, desired: bool, context: str) -> None:
        label = f"{'enable' if desired else 'disable'} midi mode {context}"
        self.ensure_checked_switch("midi", label, desired)
        self.run_step(f"wait after {label}", self.cli("wait", "--ms", "500" if desired else "250"))

    def exercise_modulation_graph_handles_for_tab(self, tab: str) -> None:
        if tab.startswith("RAND"):
            self.run_step(f"random graph drag for {tab}", self.cli("drag", "--class", "RandomGraphComponent", "--nth", "0", "--position", "80,24", "--dx", "50", "--dy", "0", "--steps", "8", "--timeout-ms", "5000"))
            return

        if tab == "INPUT":
            self.try_step("input graph drag first handle", self.cli("drag", "--class", "NodeGraphComponent", "--nth", "2", "--position", "18,90", "--dx", "20", "--dy", "-20", "--steps", "8", "--timeout-ms", "5000", "--force"))
            self.try_step("input graph drag second handle", self.cli("drag", "--class", "NodeGraphComponent", "--nth", "2", "--position", "105,44", "--dx", "-14", "--dy", "18", "--steps", "8", "--timeout-ms", "5000", "--force"))
            return

        for graph_nth in [0, 1, 2]:
            self.drag_component_percent(f"drag first graph handle for {tab} graph {graph_nth}", 8, 65, 24, -18, "--class", "NodeGraphComponent", "--nth", graph_nth)
            self.drag_component_percent(f"drag second graph handle for {tab} graph {graph_nth}", 50, 35, -12, 20, "--class", "NodeGraphComponent", "--nth", graph_nth)
            self.drag_component_percent(f"drag curve graph handle for {tab} graph {graph_nth}", 35, 45, 0, 24, "--class", "NodeGraphComponent", "--nth", graph_nth)

    def mod_drag_description(self, tab: str) -> str:
        if tab.startswith("LFO "):
            return f"MOD:LFO:{int(tab.split()[1]) - 1}"
        if tab.startswith("ENV "):
            return f"MOD:ENV:{int(tab.split()[1]) - 1}"
        if tab.startswith("RAND "):
            return f"MOD:RNG:{int(tab.split()[1]) - 1}"
        if tab == "INPUT":
            return "MOD:SC:0"
        raise StepError(f"unknown modulation tab {tab}")

    def visible_modulation_handle_index(self, tab: str, env_visible: bool) -> int | None:
        if env_visible:
            return MOD_HANDLE_INDEX.get(tab)

        if tab.startswith("ENV "):
            return None
        if tab.startswith("LFO "):
            return int(tab.split()[1]) - 1
        if tab.startswith("RAND "):
            return 8 + int(tab.split()[1]) - 1
        if tab == "INPUT":
            return 11
        return None

    def exercise_modulation_source_assignment(self, tab: str, handle_nth: int | None = None, env_visible: bool = True) -> None:
        if handle_nth is None:
            handle_nth = self.visible_modulation_handle_index(tab, env_visible)
        if handle_nth is None:
            self.die(f"No modulation handle index mapping for '{tab}'")

        drag_description = self.mod_drag_description(tab)
        self.run_step(f"select modulation tab for assignment {tab}", self.cli("click", "--class", "ModTabHandle", "--nth", handle_nth, "--force", "--timeout-ms", "3000"))
        self.run_step(f"drag modulation source {tab} to perspective", self.cli("drag-to", "--class", "ModTabHandle", "--nth", handle_nth, "--target-class", "KnobContainerComponent", "--target-nth", "1", "--force", "--steps", "16", "--timeout-ms", "6000"))
        self.run_step(f"drop modulation source {tab} on perspective", self.cli("drop", "--class", "KnobContainerComponent", "--nth", "1", "--description", drag_description, "--force", "--timeout-ms", "5000"))
        self.run_step(f"wait after modulation drop {tab}", self.cli("wait", "--ms", "250"))
        self.run_step(f"snapshot modulation assignment {tab}", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "ModTabHandle", "--nth", handle_nth))
        self.run_step(f"drag modulation strength {tab}", self.cli("drag", "--class", "DepthIndicator", "--nth", "0", "--dx", "0", "--dy", "-18", "--steps", "8", "--force", "--timeout-ms", "5000"))
        self.run_step(f"right click modulation strength {tab} and set bipolar", self.cli("right-click", "--class", "DepthIndicator", "--nth", "0", "--force", "--menu-item", "Make Bipolar", "--timeout-ms", "5000"))
        self.try_step(f"dismiss modulation context menu {tab}", self.cli("press", "Escape"))

    def exercise_modulation_tabs(self, tabs: list[str], env_visible: bool) -> None:
        for tab in tabs:
            handle_nth = self.visible_modulation_handle_index(tab, env_visible)
            if handle_nth is None:
                self.try_step(f"skip unavailable modulation tab {tab}", self.cli("wait", "--ms", "1"))
                continue
            self.try_step(f"select modulation tab {tab}", self.cli("click", "--class", "ModTabHandle", "--nth", handle_nth, "--force", "--timeout-ms", "2500"))
            self.try_step(f"describe modulation tab {tab}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "ModTabHandle", "--nth", handle_nth))
            self.exercise_modulation_graph_handles_for_tab(tab)
            self.exercise_modulation_source_assignment(tab, handle_nth, env_visible)

    def exercise_hello_world_editor(self) -> None:
        edited_text = "hello\njucewright automation\nworld"
        self.run_step("edit Hello World source text", self.cli("fill", "--class", "ErrorCodeEditorComponent", "--nth", "0", "--timeout-ms", "5000", edited_text))
        self.run_step("wait after editing Hello World source text", self.cli("wait", "--ms", "500"))
        self.run_step("snapshot after editing Hello World source text", self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "ErrorCodeEditorComponent", "--nth", "0"))
        self.run_step("collapse Hello World source editor", self.cli("click", "--role", "button", "--name", "Collapse", "--exact", "--timeout-ms", "3000"))
        self.run_step("snapshot after collapsing Hello World source editor", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))

    def try_open_effect_browser(self, effect: str) -> None:
        file = self.next_step_file(f"ensure effect browser for {effect}")
        self.log(f"TRY open effect browser for {effect}")
        if self.call_to_file(self.cli("click", "--name", "Add new effect", "--exact", "--timeout-ms", "3000"), file):
            self.log(f"OK  open effect browser for {effect} -> {file}")
        else:
            self.log(f"INFO effect browser for {effect} was already open or will be verified by the next required step -> {file}")

    def exercise_effect(self, effect: str) -> None:
        nth = EFFECT_INDEX.get(effect)
        if nth is None:
            self.die(f"No effect index mapping for '{effect}'")

        self.try_open_effect_browser(effect)
        self.run_step(f"find effect browser item {effect}", self.cli("locator", "--class", "osci::GridItemComponent", "--nth", nth, "--timeout-ms", "5000"))
        self.run_step(f"add effect {effect}", self.cli("click", "--class", "osci::GridItemComponent", "--nth", nth, "--force", "--timeout-ms", "6000"))
        self.try_step(f"wait after adding effect {effect}", self.cli("wait", "--ms", "200"))
        self.try_step(f"snapshot selected effect {effect}", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "DraggableListBox"))
        self.exercise_visible_controls(f"selected effect {effect}", 18, "--class", "DraggableListBox")

    def exercise_menu(self, menu: str) -> None:
        self.try_step(f"open menu {menu}", self.cli("click", "--role", "menuItem", "--name", menu, "--exact", "--timeout-ms", "3000"))
        self.try_step(f"snapshot menu {menu}", self.cli("snapshot", "--json", "--full", "--depth", "8"))
        self.try_step(f"close menu {menu}", self.cli("press", "Escape"))

    def close_overlay(self) -> None:
        self.call(self.cli("click", "--name", "closeOverlay", "--exact", "--timeout-ms", "3000"))
        self.call(self.cli("wait", "--ms", "250"))

    def exercise_about_dialog(self) -> None:
        self.run_step("open about dialog", self.select_menu_item("About osci-render"))
        self.run_step("about dialog snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        self.try_step("about website button trial", self.cli("click", "--name", "Website", "--exact", "--trial", "--timeout-ms", "3000"))
        self.try_step("about feedback button trial", self.cli("click", "--name", "Send Feedback", "--exact", "--trial", "--timeout-ms", "3000"))
        self.run_step("close about dialog", self.close_overlay)

    def exercise_license_dialog(self) -> None:
        self.try_step("open license and updates dialog", self.select_menu_item("License and Updates..."))
        self.try_step("license and updates snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        self.try_step("license and updates full snapshot", self.cli("snapshot", "--json", "--full", "--depth", "14"))
        self.try_step("close license and updates dialog", self.close_overlay)

    def exercise_recording_dialog(self) -> None:
        self.run_step("open recording settings dialog", self.select_menu_item("Recording Settings..."))
        self.run_step("recording settings snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        self.try_step("set recording video quality", self.cli("set-value", "--role", "slider", "--name", "Video Quality", "--exact", "--timeout-ms", "3000", "0.62"))
        self.try_step("set recording resolution", self.cli("set-value", "--role", "slider", "--name", "Resolution", "--exact", "--timeout-ms", "3000", "720"))
        self.try_step("set recording frame rate", self.cli("set-value", "--role", "slider", "--name", "Frame Rate", "--exact", "--timeout-ms", "3000", "30"))
        self.try_step("toggle lossless video", self.cli("click", "--role", "button", "--name", "Lossless Video", "--exact", "--timeout-ms", "3000"))
        self.try_step("select compression speed medium", self.cli("select-option", "--role", "comboBox", "--name", "Compression speed", "--exact", "--text", "medium", "--timeout-ms", "3000"))
        self.try_step("select video codec h265", self.cli("select-option", "--role", "comboBox", "--name", "Video codec", "--exact", "--text", "H.265/HEVC", "--timeout-ms", "3000"))
        self.exercise_visible_controls("recording settings dialog", 0, "--class", "RecordingSettingsOverlay")
        self.run_step("close recording settings dialog", self.close_overlay)

    def exercise_audio_dialog(self) -> None:
        self.run_step("open audio settings dialog", self.select_menu_item("Settings..."))
        self.run_step("wait for audio settings overlay animation", self.cli("wait", "--ms", "1200"))
        self.run_step("audio settings snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "14"))
        self.try_step("audio settings full snapshot", self.cli("snapshot", "--json", "--full", "--depth", "16"))
        self.exercise_visible_controls("audio settings dialog", 0, "--class", "AudioSettingsOverlay")
        self.run_step("close audio settings dialog", self.close_overlay)

    def exercise_visualiser_settings_dialog(self) -> None:
        self.run_step("open visualiser settings dialog", self.cli("click", "--name", "settings", "--timeout-ms", "3000"))
        self.run_step("visualiser settings snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        self.exercise_visible_controls("visualiser settings dialog", 0, "--role", "dialogWindow", "--name", "Visualiser Settings", "--exact")
        self.run_step("close visualiser settings dialog", self.cli("press", "Escape", "--role", "dialogWindow", "--name", "Visualiser Settings", "--exact", "--timeout-ms", "3000"))

    def exercise_application_dialogs(self) -> None:
        self.exercise_feedback_dialog()
        self.exercise_about_dialog()
        self.exercise_license_dialog()
        self.exercise_recording_dialog()
        self.exercise_audio_dialog()
        self.exercise_visualiser_settings_dialog()

    def configure_visualiser_recording_for_automation(self) -> None:
        self.run_step("open recording settings for visualiser recording", self.select_menu_item("Recording Settings..."))
        self.try_step("recording uses h264", self.cli("select-option", "--role", "comboBox", "--name", "Video codec", "--text", "H.264", "--timeout-ms", "3000"))
        self.try_step("recording uses ultrafast compression", self.cli("select-option", "--role", "comboBox", "--name", "Compression speed", "--text", "ultrafast", "--timeout-ms", "3000"))
        self.try_step("recording disables lossless video", self.cli("set-checked", "--role", "button", "--name", "Lossless Video", "--exact", "--timeout-ms", "3000", "false"))
        self.try_step("recording enables video", self.cli("set-checked", "--role", "button", "--name", "Record Video", "--exact", "--timeout-ms", "3000", "true"))
        self.try_step("recording disables audio mux", self.cli("set-checked", "--role", "button", "--name", "Record Audio", "--exact", "--timeout-ms", "3000", "false"))
        self.try_step("recording resolution low", self.cli("set-value", "--role", "slider", "--name", "Resolution", "--exact", "--timeout-ms", "3000", "256"))
        self.try_step("recording frame rate low", self.cli("set-value", "--role", "slider", "--name", "Frame Rate", "--exact", "--timeout-ms", "3000", "10"))
        self.try_step("recording video quality medium", self.cli("set-value", "--role", "slider", "--name", "Video Quality", "--exact", "--timeout-ms", "3000", "0.45"))
        self.run_step("close recording settings for visualiser recording", self.close_overlay)

    def exercise_visualiser_recording(self) -> None:
        marker_file = self.artifact_dir / "visualiser-recording-start.marker"

        if not self.include_native:
            self.try_step("visualiser recording skipped because stopping opens a native save dialog", lambda: print("skipped"))
            return

        self.configure_visualiser_recording_for_automation()
        marker_file.write_text("", encoding="utf-8")
        self.run_step("start visualiser recording", self.cli("click", "--name", "Record", "--exact", "--timeout-ms", "5000"))
        self.run_step("wait while recording visualiser", self.cli("wait", "--ms", "2600"))
        self.try_step("visualiser screenshot during recording", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / "visualiser_recording_active.png"))
        self.run_step("stop visualiser recording", self.cli("click", "--name", "Record", "--exact", "--timeout-ms", "5000"))
        self.try_step("cancel native recording save dialog", self.native_escape)
        self.run_step("wait after cancelling visualiser recording save dialog", self.cli("wait", "--ms", "1000"))
        self.try_step("verify visualiser recording file if native dialog saved", lambda: self.verify_new_recording_file(marker_file))

    def exercise_native_dialog_entries(self) -> None:
        self.try_step("render audio file to video native dialog trial", self.select_menu_item("Render Audio File to Video..."))
        self.try_step("close native dialog with escape", self.cli("press", "Escape"))

    def prepare_external_fixtures(self) -> None:
        self.fixture_dir.mkdir(parents=True, exist_ok=True)
        fixtures = {
            "automation.gpla": self.root_dir / "Resources" / "gpla" / "fallback.gpla",
            "automation.gif": self.root_dir / "images" / "demo1.gif",
            "automation.png": self.root_dir / "Resources" / "oscilloscope" / "real.png",
            "automation.jpg": self.root_dir / "Resources" / "oscilloscope" / "empty.jpg",
            "automation.flac": self.root_dir / "Resources" / "audio" / "sosci.flac",
            "automation.mp4": self.root_dir / "tests" / "fixtures" / "jucewright" / "cubes.mp4",
            "automation.mov": self.root_dir / "tests" / "fixtures" / "jucewright" / "cubes.mp4",
        }
        for name, source in fixtures.items():
            shutil.copyfile(source, self.fixture_dir / name)

    def exercise_external_file(self, kind: str, path: Path) -> None:
        self.launch_app(f"external {kind}")
        self.ensure_midi_mode(False, f"for external {kind}")
        self.run_step(f"drop external {kind} file", self.cli("drop-files", "--file", path, "--timeout-ms", "5000"))
        self.run_step(f"wait after dropping external {kind}", self.cli("wait", "--ms", "750"))
        self.run_step(f"snapshot external {kind}", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        screenshot = self.artifact_dir / f"visualiser_external_{slug(kind)}.png"
        self.run_step(f"visualiser screenshot external {kind}", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", screenshot))
        self.try_step(f"visualiser nonblank external {kind}", lambda: self.check_visualiser_png_not_blank(screenshot))
        self.exercise_current_file_common(f"external {kind}")
        self.exercise_frame_and_timeline_controls(f"external {kind}", kind)

    def exercise_external_file_type_passes(self) -> None:
        self.prepare_external_fixtures()
        external_types = ["gpla", "gif", "png", "jpg", "flac", "mp4", "mov"]
        if self.quick:
            external_types = ["gpla", "gif", "flac"]

        for kind in external_types:
            self.exercise_external_file(kind, self.fixture_dir / f"automation.{kind}")

    def write_summary(self) -> None:
        summary = {
            "session": self.session,
            "artifactDir": str(self.artifact_dir),
            "quick": self.quick,
            "failures": len(self.failures),
            "optionalFailures": len(self.optional_failures),
            "platform": platform.system(),
        }
        (self.artifact_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

        if self.optional_failures:
            self.log("Optional failures:")
            text = "".join(f"  - {failure}\n" for failure in self.optional_failures)
            print(text, end="")
            (self.artifact_dir / "optional-failures.txt").write_text(text, encoding="utf-8")

        if self.failures:
            self.log("Failures:")
            text = "".join(f"  - {failure}\n" for failure in self.failures)
            print(text, end="")
            (self.artifact_dir / "failures.txt").write_text(text, encoding="utf-8")
            self.log(f"Summary: {self.artifact_dir / 'summary.json'}")
        else:
            self.log(f"Completed without required failures. Summary: {self.artifact_dir / 'summary.json'}")

    def run(self) -> int:
        self.feedback_mock = FeedbackMockServer(self.artifact_dir)
        self.feedback_mock.start()
        previous_feedback_url = os.environ.get("OSCI_FEEDBACK_API_BASE_URL")
        os.environ["OSCI_FEEDBACK_API_BASE_URL"] = self.feedback_mock.base_url
        try:
            if not self.find_jucewright():
                self.build_jucewright()
                if not self.find_jucewright():
                    self.die("Could not find jucewright after build")

            if self.build_app_requested or not is_executable(self.app_executable):
                self.build_app()

            if not is_executable(self.app_executable):
                self.die(f"App executable not found: {self.app_executable}")
            if self.jucewright is None or not is_executable(self.jucewright):
                self.die(f"jucewright not executable: {self.jucewright}")

            self.log(f"Artifacts: {self.artifact_dir}")
            self.log(f"jucewright: {self.jucewright}")
            self.log(f"App: {self.app_executable}")

            self.launch_app("clean startup")

            if self.feedback_only:
                self.run_step("windows", self.cli("windows"))
                self.exercise_feedback_dialog()
                return 1 if self.failures else 0

            self.run_step("list sessions", self.jw("list"))
            self.run_step("capabilities", self.cli("capabilities"))
            self.run_step("windows", self.cli("windows"))
            self.ensure_midi_mode(False, "after clean startup")
            self.run_step("start trace", self.cli("trace-start", "--file", self.artifact_dir / "trace.json"))

            self.run_step("root full snapshot", self.cli("snapshot", "--json", "--full", "--depth", "14"))
            self.run_step("root interesting snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
            self.run_step("root screenshot", self.cli("screenshot", "--target", "root", "--source", "auto", "--file", self.artifact_dir / "root.png"))
            self.run_step("visualiser count", self.cli("count", "--class", "VisualiserComponent", "--nth", "0"))
            self.run_step("visualiser describe", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "VisualiserComponent", "--nth", "0"))
            self.run_step("visualiser screenshot", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / "visualiser.png"))
            self.run_step("visualiser screenshot nonblank check", lambda: self.check_visualiser_png_not_blank(self.artifact_dir / "visualiser.png"))

            for menu in ["File", "Edit", "About", "Video", "Audio", "Interface"]:
                self.exercise_menu(menu)

            self.exercise_application_dialogs()

            self.run_step("file controls describe", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "FileControlsComponent"))
            self.run_step("quick controls describe", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "QuickControlsBar"))
            self.run_step("volume controls describe", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "VolumeComponent"))
            self.exercise_visible_controls("quick controls root", 0, "--class", "QuickControlsBar")
            self.exercise_visible_controls("volume controls root", 0, "--class", "VolumeComponent")
            self.exercise_visible_controls("main workspace controls", 25 if self.quick else 120)
            self.ensure_midi_mode(False, "after generic workspace controls")

            text_examples = ["Hello World"] if self.quick else TEXT_EXAMPLES
            lua_examples = ["Spiral", "Shape Generator"] if self.quick else LUA_EXAMPLES
            model_examples = ["Cube"] if self.quick else MODEL_EXAMPLES
            svg_examples = ["Air Horn"] if self.quick else SVG_EXAMPLES
            fractal_examples = ["Koch Snowflake"] if self.quick else FRACTAL_EXAMPLES

            for example in model_examples:
                self.load_example(example)
                self.exercise_current_file_common(f"model {example}")

            for example in lua_examples:
                self.load_example(example, True)
                self.exercise_current_file_common(f"lua {example}")
                self.exercise_lua_file_controls(example)

            for example in svg_examples:
                self.load_example(example)
                self.exercise_current_file_common(f"svg {example}")

            for example in fractal_examples:
                self.load_example(example)
                self.exercise_current_file_common(f"fractal {example}")
                self.exercise_fractal_file_controls(example)

            for example in text_examples:
                self.load_example(example, True)
                self.exercise_current_file_common(f"text {example}")
                self.exercise_text_file_controls(example)
                if example == "Hello World":
                    self.exercise_hello_world_editor()

            self.exercise_file_switching()

            self.try_step("hidden timeline describe without animatable file", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "TimelineComponent", "--hidden"))

            self.ensure_midi_mode(False, "before non-env modulation")
            self.run_step("midi controls describe with midi disabled", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "MidiComponent"))

            self.run_step("modulation tabs snapshot with midi disabled", self.cli("locator", "--format", "json", "--class", "ModTabHandle"))
            mod_tabs = ["LFO 1", "RAND 1", "INPUT", "ENV 1"] if self.quick else MOD_TABS
            non_env_tabs = [tab for tab in mod_tabs if not tab.startswith("ENV ")]
            env_tabs = [tab for tab in mod_tabs if tab.startswith("ENV ")]
            self.exercise_modulation_tabs(non_env_tabs, False)

            self.try_step("lfo graph drag", self.cli("drag", "--class", "NodeGraphComponent", "--nth", "0", "--dx", "20", "--dy", "-20", "--steps", "8", "--timeout-ms", "3000"))
            self.try_step("modulation graph snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "NodeGraphComponent", "--nth", "0"))

            self.ensure_midi_mode(True, "for keyboard and env modulation")
            self.run_step("midi controls after enabling for keyboard and env", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "MidiComponent"))
            self.ensure_midi_keyboard_visible()
            self.exercise_midi_keyboard_clicks()
            self.run_step("modulation tabs snapshot with midi enabled", self.cli("locator", "--format", "json", "--class", "ModTabHandle"))
            self.exercise_modulation_tabs(env_tabs, True)
            self.ensure_midi_mode(False, "after keyboard and env modulation")

            self.run_step("stop startup trace before clean effects sweep", self.cli("trace-stop"))
            self.launch_app("clean effects sweep")
            self.ensure_midi_mode(False, "for clean effects sweep")
            self.run_step("start effects trace", self.cli("trace-start", "--file", self.artifact_dir / "effects-trace.json"))
            self.run_step("audio effects describe", self.cli("describe", "--json", "--interesting", "--depth", "10", "--class", "EffectsComponent"))
            self.try_open_effect_browser("initial effects sweep")
            self.run_step("effect browser snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "EffectTypeGridComponent"))

            effects = ["Bit Crush", "Rotate", "Scale"] if self.quick else EFFECTS
            for effect in effects:
                self.exercise_effect(effect)

            self.try_step("visualiser settings button", self.cli("click", "--name", "settings", "--timeout-ms", "3000"))
            self.try_step("visualiser settings snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
            self.try_step("close visualiser settings", self.cli("press", "Escape"))
            self.exercise_visualiser_recording()
            self.try_step("open visualiser window trial", self.cli("click", "--name", "new window", "--trial", "--timeout-ms", "3000"))
            self.try_step("fullscreen trial", self.cli("click", "--name", "fullscreen", "--trial", "--timeout-ms", "3000"))

            if self.include_native:
                self.try_step("native open project trial", self.cli("click", "--text", "Open Project", "--exact", "--trial", "--timeout-ms", "3000"))
                self.try_step("native save project as trial", self.cli("click", "--text", "Save Project As", "--exact", "--trial", "--timeout-ms", "3000"))
                self.exercise_native_dialog_entries()

            self.run_step("stop effects trace before external file passes", self.cli("trace-stop"))
            self.exercise_external_file_type_passes()
            self.run_step("start final trace", self.cli("trace-start", "--file", self.artifact_dir / "final-trace.json"))

            self.mcp_call("initialize", {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "osci-render-browser-script", "version": "1.0.0"}}})
            self.mcp_call("tools list", {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
            self.mcp_call("list sessions", {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "juce_list_sessions", "arguments": {}}})
            self.mcp_call("snapshot", {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "juce_snapshot", "arguments": {"session": self.session, "mode": "interesting", "depth": 10, "format": "json"}}})
            self.mcp_call("visualiser screenshot", {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "juce_screenshot", "arguments": {"session": self.session, "locator": {"class": "VisualiserComponent", "nth": 0}, "file": str(self.artifact_dir / "mcp_visualiser.png"), "includeBase64": False}}})

            self.run_step("stop trace", self.cli("trace-stop"))
            self.run_step("final root snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
            return 1 if self.failures else 0
        except Exception as exc:
            self.failures.append(f"unexpected runner error: {exc}")
            return 1
        finally:
            self.write_summary()
            self.stop_app()
            self.feedback_mock.stop()
            if previous_feedback_url is None:
                os.environ.pop("OSCI_FEEDBACK_API_BASE_URL", None)
            else:
                os.environ["OSCI_FEEDBACK_API_BASE_URL"] = previous_feedback_url
