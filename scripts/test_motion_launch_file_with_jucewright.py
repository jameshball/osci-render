#!/usr/bin/env python3
"""Verify that a project given at launch opens over a session left to recover.

The recovered session is restored first; restoring it while the file loaded
used to replace the project mid-load ("The current project changed").
"""
import os
import shutil
import subprocess
import sys
import time
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
if sys.platform != "darwin":
    print("Launch-file check runs on macOS only (it starts the app binary itself).")
    sys.exit(0)

# A prepared profile to launch from: Jucewright can't pass the app a file.
session.keep_app = False
session.launch_app("motion-launch-file")
home = session.automation_home_root / f"{session.launch_index:02d}_motion-launch-file"
session.stop_app()
time.sleep(1)
examples = session.root_dir / "Resources/motion/examples"
folder = home / "Library/Application Support/osci-render"
folder.mkdir(parents=True, exist_ok=True)
shutil.copy(examples / "First_lines.osci-motion", folder / "osci-motion recovery.osci-motion")

binary = session.app_path / "Contents/MacOS/osci-motion"
env = dict(os.environ, HOME=str(home), CFFIXED_USER_HOME=str(home), JUCEWRIGHT_AUTOMATION="1", JUCEWRIGHT_SESSION=session.session, JUCEWRIGHT_ARTIFACT_ROOT=str(session.artifact_dir))
app = subprocess.Popen([str(binary), str(examples / "Orbit.osci-motion")], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, timeout=60).stdout


try:
    for _ in range(60):
        if "MotionEditor" in command("snapshot", "--depth", "3"):
            break
        time.sleep(1)
    else:
        raise RuntimeError("The app didn't start")
    time.sleep(3)
    session.acknowledge_startup_legal()
    opened = False
    for _ in range(30):
        tree = command("snapshot")
        if "Project changed" in tree or "changed while loading" in tree:
            raise RuntimeError("The file given at launch didn't open: the recovered session replaced it as it loaded")
        # Orbit's floor: First lines has no such track.
        if 'componentName=Track name' in tree and '"Floor"' in tree:
            opened = True
            break
        time.sleep(1)
    command("screenshot", "--file", session.artifact_dir / "launch-file.png")
    if not opened:
        raise RuntimeError("The file given at launch never opened")
    print("Motion launch-file smoke passed")
finally:
    app.terminate()
