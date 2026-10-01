#!/usr/bin/env python3
"""Author "dah", the osci-motion benchmark film, through the application UI.

Every source, clip, property, key, modulation, effect, group, camera, cut and
MIDI assignment is created with user-facing controls. Only the empty starting
project file and the generated source media come from outside the app.

Run scripts/create_motion_dah2_assets.py build/dah/assets first.
Usage: PYTHONPATH=scripts python3 scripts/author_motion_dah2_with_jucewright.py --app <osci-motion.app> [--until PHASE] [--keep-app]
"""
import json
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

def option(flag):
    if flag not in sys.argv:
        return None
    index = sys.argv.index(flag)
    value = sys.argv[index + 1]
    del sys.argv[index:index + 2]
    return value


until = option("--until")
resume = option("--resume")      # continue a saved project ...
start_from = option("--from")    # ... beginning at this phase

from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

ASSETS = Path("build/dah/assets").resolve()
BAR = 1.6
BEAT = 0.4


def bar(n):
    return round(n * BAR, 6)


session = BrowserSession(parse_args())
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("dah-authoring")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def snapshot():
    return json.loads(command("snapshot", "--json", "--full"))


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def find(predicate, tree=None):
    return next((node for node in nodes(tree if tree is not None else snapshot()) if predicate(node)), None)


def by_component_id(identity):
    found = find(lambda node: node.get("componentId") == identity and node.get("visible"))
    assert found is not None, identity
    return found


def edit_label(name, value):
    """Double-click a JUCE label, type into its editor and commit."""
    target = find(lambda node: node.get("componentName") == name and node.get("visible"))
    assert target is not None, name
    step("open " + name, "click", target["ref"], "--click-count", 2)
    target = find(lambda node: node.get("componentName") == name)
    editor = find(lambda node: node.get("class") == "juce::TextEditor", target)
    assert editor is not None, "editor for " + name
    step(f"{name} = {value}", "fill", editor["ref"], str(value))
    step("commit " + name, "press", "Return")


def seek(seconds):
    edit_label("Timeline position", f"{seconds:.4f}s")


def tab(name, bar_name="Inspector tabs"):
    """Tabs repeat names across bars (Effects), so resolve within one bar."""
    if name in ("Timeline", "Graph", "Notes"):
        step("tab " + name, "click", "--name", name, "--class", "osci::TabBar::Tab", "--exact")
        return
    owner = find(lambda node: node.get("class") == "osci::TabBar" and node.get("name") == bar_name)
    target = find(lambda node: node.get("class") == "osci::TabBar::Tab" and node.get("name") == name, owner)
    assert target is not None, name
    step("tab " + name, "click", target["ref"])


def set_property(prop, value, prefix=""):
    field = by_component_id("motion." + prefix.replace(" ", ".") + prop)
    step(f"{prefix}{prop} = {value}", "set-value", field["ref"], str(value))


def set_properties(values, prefix=""):
    for prop, value in values.items():
        set_property(prop, value, prefix)


def save():
    step("save", "press", "command + s", "--class", "MotionEditor")


def import_source(name, start, duration=None, bake=None, offset=None):
    """Import at the playhead, which inserts one clip on a new track."""
    seek(start)
    step("import " + name, "drop-files", "--file", ASSETS / name, "--class", "MotionEditor", "--exact")
    if name.endswith(".lua"):
        command("wait-for-locator", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact")
        step("bake length", "fill", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact", str(bake))
        step("bake " + name, "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    elif name.endswith(".gif"):
        command("wait-for-locator", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
        step("trace " + name, "click", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
    label = "Undo Import soundtrack" if name.endswith(".flac") else "Undo Import MIDI file" if name.endswith(".mid") else "Undo Import object"
    command("wait-for-locator", "--name", label, "--role", "label", "--exact", "--timeout-ms", 120000)
    timing(duration=duration, offset=offset, back="Properties")


def timing(start=None, duration=None, offset=None, back="Properties"):
    if start is None and duration is None and offset is None:
        return
    tab("Timing")
    if start is not None:
        edit_label("Clip start", start)
    if duration is not None:
        edit_label("Clip duration", duration)
    if offset is not None:
        edit_label("Clip source offset", offset)
    tab(back)


def duplicate_to(start, duration=None, offset=None):
    step("duplicate", "press", "command + d", "--class", "MotionEditor")
    timing(start=start, duration=duration, offset=offset)


animated = set()


def keys(track, group, moments, prefix=""):
    """moments: [(time, {prop: value})]. First key toggles the row; later values key themselves."""
    for moment, values in moments:
        seek(moment)
        set_properties(values, prefix)
        if (track, group) not in animated:
            step(f"key {group} on {track}", "click", "--name", "Key " + prefix + group.lower(), "--exact")
            animated.add((track, group))


def modulate(label, waveform, amount, beats=None, hertz=None, mode="Add"):
    """Graph tab: choose the property, then configure its modulation."""
    tab("Graph")
    step("graph " + label, "click", "--name", "Curve " + label, "--exact")
    enable = find(lambda node: node.get("componentName") == "Enable modulation")
    if not enable.get("checked", enable.get("toggleState", False)):
        step("enable modulation", "click", enable["ref"])
    step("waveform " + waveform, "select-option", "--name", "Modulation waveform", "--text", waveform)
    step("mode " + mode, "select-option", "--name", "Modulation mode", "--text", mode)
    if waveform != "Soundtrack loudness":
        step("clock", "select-option", "--name", "Modulation clock", "--text", "Beats" if beats is not None else "Hz")
        step("rate", "set-value", "--name", "Modulation rate", "--role", "slider", str(beats if beats is not None else hertz))
    step("amount", "set-value", "--name", "Modulation amount", "--role", "slider", str(amount))
    tab("Timeline")


def library_tab(name):
    library = find(lambda node: node.get("class") == "osci::TabBar" and node.get("name") == "Library tabs")
    target = find(lambda node: node.get("class") == "osci::TabBar::Tab" and node.get("name") == name, library)
    step("library " + name, "click", target["ref"])


def add_effect(effect, parameters=None, scope=None):
    tab("Effects")
    if scope is not None:
        step("scope " + scope, "select-option", "--name", "Effect scope", "--text", scope)
    library_tab("Effects")
    step("add " + effect, "click", "--name", effect, "--role", "listItem", "--click-count", 2)
    for parameter, value in (parameters or {}).items():
        step(f"{effect} {parameter}", "set-value", "--name", "Effect " + parameter, "--role", "slider", str(value))
    library_tab("Assets")
    tab("Properties")


def rename_track(name):
    """Name the selected clip's track (imports reveal and select their row)."""
    labels = [node for node in nodes(snapshot()) if str(node.get("componentName", "")).startswith("Track name ") and node.get("visible")]
    newest = max(labels, key=lambda node: int(node["componentName"].split()[-1]))
    edit_label(newest["componentName"], name)
    return int(newest["componentName"].split()[-1])


def select_uses(asset):
    """Select every clip of a source from its library context menu."""
    search = find(lambda node: node.get("componentName") == "Search sources")
    step("search " + asset, "fill", search["ref"], asset)
    step("reveal " + asset, "click", "--name", asset, "--role", "listItem", "--exact", "--button", "right")
    step("select uses of " + asset, "click", "--name", "Select 1 clip using it", "--role", "menuItem", "--exact")
    search = find(lambda node: node.get("componentName") == "Search sources")
    step("clear search", "fill", search["ref"], "")


def camera(name, values, moments=None):
    tab("Camera")
    step("add camera " + name, "click", "--name", "Add", "--class", "juce::TextButton", "--exact")
    edit_label("Camera name", name)
    set_properties(values, "camera ")
    for group, keyed in (moments or {}).items():
        keys(name, group, keyed, "camera ")
    tab("Properties")


def cut(seconds, name):
    tab("Camera")
    seek(seconds)
    step("edit " + name, "select-option", "--name", "Camera to edit", "--text", name)
    step("cut to " + name, "click", "--name", "Cut here", "--exact")
    tab("Properties")


def key_effect(parameter, moments):
    for moment, value in moments:
        seek(moment)
        step(f"effect {parameter} = {value}", "set-value", "--name", "Effect " + parameter, "--role", "slider", str(value))
        if moment == moments[0][0]:
            step("key effect " + parameter, "click", "--name", "Key effect " + parameter, "--exact")


def words(asset, first, every, until, duration, values, track):
    """A type hit on a fixed beat, repeated every few bars."""
    import_source(asset, first, duration=duration)
    rename_track(track)
    set_properties(values)
    moment = first + every
    while moment < until - 1e-6:
        duplicate_to(round(moment, 6))
        moment += every


# ---------------------------------------------------------------------------
# Phases. Each builds on the previous ones; --until stops after a phase.

def setup():
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    global project
    if resume is not None:
        project = session.artifact_dir / "dah.osci-motion"
        project.write_bytes(Path(resume).read_bytes())
        subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
        command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "dah", "--timeout-ms", 60000)
        step("workspace", "resize-window", "--w", 1440, "--h", 900)
        return
    root = ET.Element("motion-project", schema="1")
    ET.SubElement(root, "composition", name="dah", duration="185.45", bpm="150", fps="30")
    xml = ET.tostring(root, encoding="utf-8")
    project = session.artifact_dir / "dah.osci-motion"
    project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "dah", "--timeout-ms", 30000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    step("canvas", "click", "--component-name", "Output canvas", "--class", "juce::TextButton", "--exact")
    step("landscape", "select-option", "--name", "Canvas preset", "--exact", "--index", 2)
    step("apply canvas", "click", "--name", "Apply canvas", "--exact")
    # The track starts 0.15 s before its first downbeat: slip it so bars fall on 1.6 s.
    import_source("dah 48k.flac", 0, duration=185.45, offset=0.15)
    save()


def intro():
    # A single point of light breathes on the beat, becomes a flat trace,
    # the trace starts to sing, and the title types itself in.
    import_source("Dot.svg", 0, duration=bar(2))
    set_properties({"scale.x": 0.025, "scale.y": 0.025, "scale.z": 0.025, "red": 0.5, "green": 1, "blue": 0.7})
    modulate("Drawing weight", "Sine", 0.6, beats=1, mode="Multiply")
    import_source("Trace.lua", bar(2), duration=bar(5), bake=12.8)
    set_properties({"red": 0.4, "green": 1, "blue": 0.55})
    keys("Trace", "Drawing", [(bar(2), {"weight": 0}), (bar(2.5), {"weight": 1}), (bar(6.5), {"weight": 1}), (bar(7), {"weight": 0})])
    import_source("dahhhhh.txt", bar(4), duration=bar(4))
    set_properties({"position.y": -0.55, "scale.y": 0.16, "scale.z": 0.16, "red": 0.85, "green": 0.9, "blue": 0.8})
    keys("Title", "Scale", [(bar(4), {"scale.x": 0.0}), (bar(5), {"scale.x": 0.16})])
    keys("Title", "Drawing", [(bar(7), {"weight": 1}), (bar(8), {"weight": 0})])
    save()


def mouths():
    # The singer: lips open on every beat. A second, magenta mouth answers on
    # the off-beats; both return as a duet to open the finale.
    import_source("Mouth.lua", bar(7), duration=bar(30) - bar(7), bake=1.6)
    rename_track("Mouth")
    set_properties({"position.y": 0.05, "position.z": 0.3, "scale.x": 1.1, "scale.y": 1.1, "scale.z": 1.1})
    keys("Mouth", "Drawing", [(bar(7), {"weight": 0}), (bar(7.5), {"weight": 2.5})])
    duplicate_to(bar(80), duration=bar(96) - bar(80))
    set_properties({"position.x": -0.42, "position.y": 0, "scale.x": 0.62, "scale.y": 0.62, "scale.z": 0.62})
    import_source("Mouth.lua", bar(22), duration=bar(30) - bar(22), bake=1.6, offset=BEAT / 2)
    rename_track("Answer")
    set_properties({"position.x": 0.72, "position.y": 0.36, "position.z": 0.3, "scale.x": 0.55, "scale.y": 0.55, "scale.z": 0.55, "weight": 2.2})
    add_effect("Colour", {"hue": -80, "saturation": 1.3})
    duplicate_to(bar(80), duration=bar(96) - bar(80))
    set_properties({"position.x": 0.42, "position.y": 0, "scale.x": 0.62, "scale.y": 0.62, "scale.z": 0.62})
    save()


def type_hits():
    # "do" on the downbeat, "da" on beat two, "dahh!" on beat four.
    groove = {"red": 0.35, "green": 1, "blue": 0.6, "scale.x": 0.15, "scale.y": 0.15, "scale.z": 0.15, "weight": 1.4}
    words("do.txt", bar(10), bar(2), bar(30), BEAT, dict(groove, **{"position.x": -0.7, "position.y": 0.62}), "do")
    duplicate_to(bar(80), duration=BEAT)
    words_after = bar(84)
    while words_after < bar(94):
        duplicate_to(words_after)
        words_after += bar(4)
    words("da.txt", bar(10) + BEAT, bar(2), bar(30), BEAT, dict(groove, **{"position.x": 0.7, "position.y": 0.62}), "da")
    duplicate_to(bar(80) + BEAT, duration=BEAT)
    words_after = bar(84) + BEAT
    while words_after < bar(94):
        duplicate_to(words_after)
        words_after += bar(4)
    words("dahh!.txt", bar(10) + 3 * BEAT, bar(2), bar(30), 2 * BEAT,
          {"position.y": -0.66, "scale.x": 0.24, "scale.y": 0.24, "scale.z": 0.24, "red": 1, "green": 0.62, "blue": 0.25, "weight": 1.6}, "dahh!")
    # The drop lands on one huge "dahh!".
    duplicate_to(bar(30), duration=bar(1))
    set_properties({"position.y": 0, "scale.x": 0.55, "scale.y": 0.55, "scale.z": 0.55})
    duplicate_to(bar(80) + 3 * BEAT, duration=2 * BEAT)
    set_properties({"position.y": -0.66, "scale.x": 0.24, "scale.y": 0.24, "scale.z": 0.24})
    words_after = bar(84) + 3 * BEAT
    while words_after < bar(94):
        duplicate_to(words_after)
        words_after += bar(4)
    save()


def ornaments():
    import_source("Lissajous.lua", bar(12), duration=bar(30) - bar(12), bake=6.4)
    rename_track("Figure left")
    set_properties({"position.x": -0.8, "position.y": -0.52, "scale.x": 0.26, "scale.y": 0.26, "scale.z": 0.26, "weight": 0.8})
    import_source("Lissajous.lua", bar(12), duration=bar(30) - bar(12), bake=6.4, offset=3.2)
    rename_track("Figure right")
    set_properties({"position.x": 0.8, "position.y": -0.52, "scale.x": 0.26, "scale.y": 0.26, "scale.z": 0.26, "weight": 0.8})
    import_source("Equaliser.gif", bar(16), duration=bar(30) - bar(16))
    rename_track("Meter")
    set_properties({"position.y": 0.82, "scale.x": 0.13, "scale.y": 0.13, "scale.z": 0.13, "red": 0.35, "green": 1, "blue": 0.6, "weight": 0.7})
    duplicate_to(bar(80), duration=bar(88) - bar(80))
    save()


def drop():
    # Into the tunnel: rings rush past (a two-bar saw that loops seamlessly
    # because the rings repeat every 2 units), stars stream, and the letters
    # at the far end pulse with the track's loudness.
    import_source("Tunnel.obj", bar(30), duration=bar(48) - bar(30))
    rename_track("Tunnel")
    # Sources are normalised on import: restore the corridor's 14-unit depth.
    set_properties({"scale.x": 7, "scale.y": 7, "scale.z": 7, "red": 0.3, "green": 0.9, "blue": 1, "weight": 0.8})
    modulate("Position Z", "Saw", 1, beats=8)
    modulate("Rotation Z", "Saw", 180, beats=16)
    add_effect("Vortex", {"strength": 0.35})
    duplicate_to(bar(88), duration=bar(112) - bar(88))
    import_source("Stars.obj", bar(30), duration=bar(48) - bar(30))
    rename_track("Stars")
    set_properties({"scale.x": 3, "scale.y": 3, "scale.z": 3, "red": 0.85, "green": 0.95, "blue": 1, "weight": 0.7})
    modulate("Position Z", "Saw", 3, beats=4)
    duplicate_to(bar(88), duration=bar(112) - bar(88))
    import_source("DAHH.obj", bar(34), duration=bar(48) - bar(34))
    rename_track("Letters")
    set_properties({"position.z": -2.5, "scale.x": 1.2, "scale.y": 1.2, "scale.z": 1.2, "red": 1, "green": 0.8, "blue": 0.55, "weight": 1.8})
    for axis in ("X", "Y", "Z"):
        modulate("Scale " + axis, "Soundtrack loudness", 0.9, mode="Multiply")
    modulate("Rotation Y", "Sine", 25, beats=8)
    duplicate_to(bar(96), duration=bar(112) - bar(96))
    save()


def breakdown():
    # Stillness: a slowly tumbling knot inside a breathing spirograph, with a
    # halo that rings out the MIDI melody.
    import_source("Knot.obj", bar(48), duration=bar(80) - bar(48))
    rename_track("Knot")
    set_properties({"scale.x": 0.55, "scale.y": 0.55, "scale.z": 0.55, "red": 0.75, "green": 0.45, "blue": 1})
    modulate("Rotation X", "Saw", 180, beats=48)
    modulate("Rotation Y", "Saw", 180, beats=32)
    keys("Knot", "Drawing", [(bar(48), {"weight": 0}), (bar(50), {"weight": 1.2}), (bar(78), {"weight": 1.2}), (bar(80), {"weight": 0})])
    add_effect("Ripple", {"rippleDepth": 0.12, "rippleAmount": 0.08})
    import_source("Spirograph.lua", bar(52), duration=bar(78) - bar(52), bake=12.8)
    rename_track("Spirograph")
    set_properties({"position.z": -0.6, "scale.x": 1.1, "scale.y": 1.1, "scale.z": 1.1, "weight": 0.35})
    keys("Spirograph", "Drawing", [(bar(52), {"weight": 0}), (bar(54), {"weight": 0.35})])
    import_source("Ring.svg", bar(56), duration=bar(72) - bar(56))
    rename_track("Halo")
    set_properties({"scale.x": 0.72, "scale.y": 0.72, "scale.z": 0.72, "red": 1, "green": 0.9, "blue": 0.75})
    step("import melody", "drop-files", "--file", ASSETS / "Melody.mid", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Undo Import MIDI file", "--role", "label", "--exact", "--timeout-ms", 60000)
    step("select melody", "click", "--name", "Melody.mid", "--role", "listItem", "--exact")
    step("assign melody", "click", "--name", "Assign to selected clip", "--exact")
    command("wait-for-locator", "--name", "Undo Assign MIDI performance", "--role", "label", "--exact", "--timeout-ms", 30000)
    tab("Timeline")
    save()


def endings():
    select_uses("Dot.svg")
    duplicate_to(bar(112), duration=185.45 - bar(112))
    keys("Dot", "Drawing", [(bar(112), {"weight": 1}), (184.4, {"weight": 0})])
    import_source("dah..txt", bar(112.5), duration=bar(115) - bar(112.5))
    rename_track("Sign-off")
    set_properties({"position.y": -0.3, "scale.x": 0.1, "scale.y": 0.1, "scale.z": 0.1, "red": 0.85, "green": 0.9, "blue": 0.8})
    save()


def cameras():
    # Wide frames the stage and pushes in through the groove; Tunnel sits
    # inside the rings; Orbit circles the knot; Close is a tighter lens.
    camera("Wide", {"position.z": 4}, {"Position": [(bar(8), {"position.z": 4}), (bar(30), {"position.z": 3.4}), (bar(80), {"position.z": 4})]})
    camera("Tunnel", {"position.z": 1.6, "fov": 62})
    # A gentle sway around the knot keeps the flat spirograph readable.
    import math
    angles = [-35, 0, 35, 0, -35]
    orbit = [(bar(48 + 8 * i), {"position.x": round(4.5 * math.sin(math.radians(a)), 4), "position.y": 0.4,
                                "position.z": round(4.5 * math.cos(math.radians(a)), 4)}) for i, a in enumerate(angles)]
    yaw = [(bar(48 + 8 * i), {"rotation.x": -5, "rotation.y": a}) for i, a in enumerate(angles)]
    camera("Orbit", {}, {"Position": orbit, "Rotation": yaw})
    camera("Close", {"position.z": 2.7})
    for moment, name in [(0, "Wide"), (bar(30), "Tunnel"), (bar(38), "Close"), (bar(40), "Tunnel"), (bar(46), "Wide"),
                         (bar(48), "Orbit"), (bar(80), "Wide"), (bar(84), "Close"), (bar(88), "Tunnel"), (bar(92), "Close"),
                         (bar(96), "Tunnel"), (bar(104), "Close"), (bar(106), "Tunnel"), (bar(110), "Close"), (bar(112), "Wide")]:
        cut(moment, name)
    save()


def finale_colour():
    # Once everything is back, the whole picture drifts through hue.
    tab("Effects")
    step("scope composition", "select-option", "--name", "Effect scope", "--text", "Composition")
    library_tab("Effects")
    step("add colour", "click", "--name", "Colour", "--role", "listItem", "--click-count", 2)
    key_effect("strength", [(bar(96), 0), (bar(100), 1), (bar(111), 1), (bar(112), 0)])
    key_effect("hue", [(bar(96), 0), (bar(112), 170)])
    library_tab("Assets")
    step("scope clip", "select-option", "--name", "Effect scope", "--text", "Clip")
    tab("Properties")
    save()


PHASES = [("setup", setup), ("intro", intro), ("mouths", mouths), ("type", type_hits),
          ("ornaments", ornaments), ("drop", drop), ("breakdown", breakdown), ("endings", endings),
          ("cameras", cameras), ("colour", finale_colour)]
try:
    skipping = start_from is not None
    for name, phase in PHASES:
        if name == start_from:
            skipping = False
        if skipping and name != "setup":
            continue
        print(f"== phase {name}", flush=True)
        phase()
        if until == name:
            break
    save()
    print("Authored project:", project, flush=True)
finally:
    session.stop_app()
