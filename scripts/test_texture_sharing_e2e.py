#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import struct
import subprocess
import sys
import time
import traceback
import wave
from pathlib import Path
from types import SimpleNamespace

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from jucewright_osci_browser.errors import StepError
from jucewright_osci_browser.platform_support import is_executable, is_macos, is_windows, user_cache_root
from jucewright_osci_browser.session import BrowserSession
from jucewright_osci_browser.utils import slug, walk_tree


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify local texture sharing between two osci-render standalone instances.")
    parser.add_argument("--transport", choices=["auto", "syphon", "spout"], default="auto", help="Texture transport under test.")
    parser.add_argument("--build-app", action="store_true", help="Resave the Projucer project and build the Debug standalone app first.")
    parser.add_argument("--keep-app", action="store_true", help="Leave the launched sender and receiver processes running.")
    parser.add_argument("--artifact-dir", help="Write logs, JSON snapshots, screenshots, and summary to this directory.")
    parser.add_argument("--jucewright", help="Use a specific jucewright executable.")
    parser.add_argument("--app", "--app-path", "--app-bundle", dest="app_path", help="Use a specific app bundle or executable.")
    parser.add_argument("--app-executable", help="Executable path used only for preflight existence checks.")
    parser.add_argument("--sender-session", default="texture-e2e-sender", help="Jucewright session name for the publishing app.")
    parser.add_argument("--receiver-session", default="texture-e2e-receiver", help="Jucewright session name for the receiving app.")
    parser.add_argument("--source-prefix", default="osci-render -", help="Popup menu item text/prefix used to select the sender source.")
    parser.add_argument("--fixture-seconds", type=float, default=20.0, help="Duration of the generated WAV fixture.")
    parser.add_argument("--skip-settings-reconnect", action="store_true", help="Only verify publish/select; skip the Recording Settings reconnect check.")
    parser.add_argument("--skip-source-removal", action="store_true", help="Skip the sender-disabled source removal check.")
    return parser.parse_args()


class TextureSharingE2ERun:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.transport = self.resolve_transport(args.transport)
        default_root = user_cache_root() / "osci-render" / "osci-render-jucewright-automation" / f"{self.transport}-texture-e2e"
        default_artifact_dir = default_root / dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.artifact_dir = Path(args.artifact_dir or os.environ.get("ARTIFACT_DIR", default_artifact_dir)).resolve()
        self.artifact_dir.mkdir(parents=True, exist_ok=True)
        self.steps_dir = self.artifact_dir / "steps"
        self.steps_dir.mkdir(parents=True, exist_ok=True)
        self.step = 0

        self.sender = self.make_session(args.sender_session, self.artifact_dir / "sender")
        self.receiver = self.make_session(args.receiver_session, self.artifact_dir / "receiver")
        self.summary: dict[str, object] = {
            "artifactDir": str(self.artifact_dir),
            "transport": self.transport,
            "senderSession": args.sender_session,
            "receiverSession": args.receiver_session,
        }

    @staticmethod
    def resolve_transport(value: str) -> str:
        if value != "auto":
            return value
        if is_macos():
            return "syphon"
        if is_windows():
            return "spout"
        raise SystemExit("Texture sharing E2E is supported only on macOS and Windows.")

    def make_session(self, session: str, artifact_dir: Path) -> BrowserSession:
        session_args = SimpleNamespace(
            session=session,
            artifact_dir=str(artifact_dir),
            app_path=self.args.app_path,
            app_executable=self.args.app_executable,
            jucewright=self.args.jucewright,
            build_app=self.args.build_app,
            quick=False,
            keep_app=self.args.keep_app,
            native_dialogs=False,
        )
        return BrowserSession(session_args)

    def log(self, message: str) -> None:
        try:
            print(f"[{self.transport}-texture-e2e] {message}", flush=True)
        except OSError:
            pass

    def step_file(self, label: str, suffix: str = ".log") -> Path:
        self.step += 1
        return self.steps_dir / f"{self.step:03d}_{slug(label)}{suffix}"

    def run_command(self, label: str, command: list[str], stdin: str | None = None, retries: int = 1, retry_delay: float = 0.5) -> str:
        log_file = self.step_file(label)
        last_output = ""
        command = [str(part) for part in command]

        for attempt in range(1, retries + 1):
            try:
                completed = subprocess.run(command,
                                           cwd=self.sender.root_dir,
                                           input=stdin,
                                           text=True,
                                           stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT,
                                           timeout=180)
            except OSError as exc:
                last_output = "Command: " + json.dumps(command) + "\n" + repr(exc) + "\n"
                log_file.write_text(last_output, encoding="utf-8")
                raise
            except subprocess.TimeoutExpired as exc:
                output = exc.stdout or ""
                last_output = "Command timed out: " + json.dumps(command) + "\n" + output
                log_file.write_text(last_output, encoding="utf-8")
                raise StepError(f"{label} timed out after 180 seconds; see {log_file}") from exc

            last_output = completed.stdout or ""
            if completed.returncode == 0:
                log_file.write_text(last_output, encoding="utf-8")
                self.log(f"OK  {label} -> {log_file}")
                return last_output

            if attempt < retries:
                time.sleep(retry_delay)

        log_file.write_text(last_output, encoding="utf-8")
        self.log(f"FAIL {label} -> {log_file}")
        raise StepError(f"{label} failed; see {log_file}")

    def run_session_command(self, session: BrowserSession, label: str, *args: object, retries: int = 1, retry_delay: float = 0.5) -> str:
        return self.run_command(label, session.cli(*args), retries=retries, retry_delay=retry_delay)

    def write_wav_fixture(self) -> Path:
        fixtures_dir = self.artifact_dir / "fixtures"
        fixtures_dir.mkdir(parents=True, exist_ok=True)
        wav_file = fixtures_dir / f"{self.transport}_texture_input.wav"
        sample_rate = 44100
        fixture_seconds = self.args.fixture_seconds
        frames = max(1, int(sample_rate * fixture_seconds))
        period = 2048

        with wave.open(str(wav_file), "wb") as wav:
            wav.setnchannels(2)
            wav.setsampwidth(2)
            wav.setframerate(sample_rate)
            for i in range(frames):
                phase = (i % period) / period
                if phase < 0.25:
                    x = -1.0 + 8.0 * phase
                    y = -1.0
                elif phase < 0.5:
                    x = 1.0
                    y = -1.0 + 8.0 * (phase - 0.25)
                elif phase < 0.75:
                    x = 1.0 - 8.0 * (phase - 0.5)
                    y = 1.0
                else:
                    x = -1.0
                    y = 1.0 - 8.0 * (phase - 0.75)

                left = int(26000 * x)
                right = int(26000 * y)
                wav.writeframesraw(struct.pack("<hh", left, right))

        self.summary["fixture"] = str(wav_file)
        self.summary["fixtureSeconds"] = fixture_seconds
        return wav_file

    @staticmethod
    def node_texts(tree: dict) -> list[str]:
        texts: list[str] = []
        for node in walk_tree(tree):
            for key in ("name", "title", "text", "value", "componentName", "class"):
                value = str(node.get(key, "")).strip()
                if value:
                    texts.append(value)
        return texts

    def snapshot(self, session: BrowserSession, label: str, file_name: str, *args: object) -> dict:
        file = self.artifact_dir / file_name
        output = self.run_session_command(session, label, "snapshot", "--json", "--interesting", "--depth", "14", *args)
        file.write_text(output, encoding="utf-8")
        return json.loads(output)

    def screenshot(self, session: BrowserSession, label: str, file_name: str) -> Path:
        file = session.artifact_dir / file_name
        self.run_session_command(session, label, "screenshot", "--target", "root", "--source", "auto", "--file", file, "--timeout-ms", "7000")
        return file

    def snapshot_contains(self, snapshot: dict, text: str) -> bool:
        return any(text in value for value in self.node_texts(snapshot.get("tree", {})))

    def has_texture_input_label(self, snapshot: dict) -> bool:
        return any(value.startswith("Using texture input:") for value in self.node_texts(snapshot.get("tree", {})))

    def has_connection_lost_alert(self, snapshot: dict) -> bool:
        texts = self.node_texts(snapshot.get("tree", {}))
        return any("Texture Input" in value for value in texts) and any("Connection lost" in value for value in texts)

    def has_source_removed_alert(self, snapshot: dict) -> bool:
        texts = self.node_texts(snapshot.get("tree", {}))
        return any("Texture Input" in value for value in texts) and any("source" in value.lower() and "removed" in value.lower() for value in texts)

    def has_publish_failed_alert(self, snapshot: dict) -> bool:
        texts = self.node_texts(snapshot.get("tree", {}))
        return any("Texture Output" in value for value in texts) and any("Publish failed" in value for value in texts)

    def texture_input_labels(self, snapshot: dict) -> list[str]:
        return [value for value in self.node_texts(snapshot.get("tree", {})) if "texture input" in value.lower() or value == "No file open"]

    def require_texture_input_label(self, snapshot: dict, label: str) -> None:
        labels = self.texture_input_labels(snapshot)
        self.summary[label] = labels
        if not any(value.startswith("Using texture input:") for value in labels):
            raise StepError(f"{label} did not show active texture input; labels={labels}")

    def require_source_removed_state(self, snapshot: dict, label: str) -> None:
        labels = self.texture_input_labels(snapshot)
        removed_alert = self.has_source_removed_alert(snapshot)
        active_label = any(value.startswith("Using texture input:") for value in labels)
        self.summary[label] = {
            "labels": labels,
            "sourceRemovedAlert": removed_alert,
            "activeTextureInputLabel": active_label,
        }
        if active_label or not removed_alert:
            raise StepError(f"{label} did not show source removal state; labels={labels}, removedAlert={removed_alert}")

    def visualiser_bounds(self, snapshot: dict) -> dict[str, int]:
        for node in walk_tree(snapshot.get("tree", {})):
            if node.get("class") == "VisualiserComponent":
                bounds = node.get("bounds", {})
                x = int(bounds.get("x", 0))
                y = int(bounds.get("y", 0))
                w = int(bounds.get("w", 0))
                h = int(bounds.get("h", 0))
                bottom = y + h
                for child in node.get("children", []):
                    child_bounds = child.get("bounds", {})
                    child_y = int(child_bounds.get("y", 0))
                    if y + (h // 2) < child_y < bottom:
                        bottom = min(bottom, child_y)

                return {
                    "x": x,
                    "y": y,
                    "w": w,
                    "h": max(1, bottom - y - 8),
                }

        raise StepError("Could not find VisualiserComponent bounds in snapshot")

    def green_image_metrics(self, image_file: Path, bounds: dict[str, int]) -> dict[str, object]:
        try:
            from PIL import Image
        except ImportError as exc:
            raise StepError("Pillow is required for texture screenshot comparison") from exc

        image = Image.open(image_file).convert("RGBA")
        left = max(0, bounds["x"])
        top = max(0, bounds["y"])
        right = min(image.width, left + max(0, bounds["w"]))
        bottom = min(image.height, top + max(0, bounds["h"]))
        if right <= left or bottom <= top:
            raise StepError(f"Invalid visualiser bounds for {image_file}: {bounds}")

        crop = image.crop((left, top, right, bottom))
        min_x = crop.width
        min_y = crop.height
        max_x = -1
        max_y = -1
        green_pixels = 0
        strong_green_pixels = 0
        max_green = 0
        green_sum = 0
        quadrants = [0, 0, 0, 0]

        pixels = crop.load()
        for y in range(crop.height):
            for x in range(crop.width):
                red, green, blue, alpha = pixels[x, y]
                if alpha > 0 and green >= 35 and green > red * 1.25 and green > blue * 1.1:
                    min_x = min(min_x, x)
                    min_y = min(min_y, y)
                    max_x = max(max_x, x)
                    max_y = max(max_y, y)
                    max_green = max(max_green, green)
                    green_sum += green
                    if green >= 128:
                        strong_green_pixels += 1
                    quadrant = (1 if x >= crop.width / 2 else 0) + (2 if y >= crop.height / 2 else 0)
                    quadrants[quadrant] += 1
                    green_pixels += 1

        metrics: dict[str, object] = {
            "image": str(image_file),
            "visualiserBounds": bounds,
            "cropSize": [crop.width, crop.height],
            "greenPixels": green_pixels,
            "greenCoverage": green_pixels / max(1, crop.width * crop.height),
            "strongGreenPixels": strong_green_pixels,
            "maxGreen": max_green,
            "quadrants": quadrants,
        }
        if green_pixels == 0:
            return metrics

        bbox_area = (max_x - min_x + 1) * (max_y - min_y + 1)
        metrics.update(
            {
                "greenBounds": [min_x, min_y, max_x, max_y],
                "greenWidth": max_x - min_x + 1,
                "greenHeight": max_y - min_y + 1,
                "greenBoundsArea": bbox_area,
                "greenBoundsDensity": green_pixels / max(1, bbox_area),
                "greenCenter": [(min_x + max_x) / 2.0, (min_y + max_y) / 2.0],
                "meanGreen": green_sum / max(1, green_pixels),
            }
        )
        return metrics

    @staticmethod
    def bbox_iou(first: list[int], second: list[int]) -> float:
        left = max(first[0], second[0])
        top = max(first[1], second[1])
        right = min(first[2], second[2])
        bottom = min(first[3], second[3])
        if right < left or bottom < top:
            return 0.0

        intersection = (right - left + 1) * (bottom - top + 1)
        first_area = (first[2] - first[0] + 1) * (first[3] - first[1] + 1)
        second_area = (second[2] - second[0] + 1) * (second[3] - second[1] + 1)
        return intersection / max(1, first_area + second_area - intersection)

    def write_visual_diagnostic_images(
        self,
        label: str,
        sender_screenshot: Path,
        sender_bounds: dict,
        receiver_screenshot: Path,
        receiver_bounds: dict,
    ) -> dict[str, str]:
        from PIL import Image, ImageDraw

        def crop(path: Path, bounds: dict) -> Image.Image:
            image = Image.open(path).convert("RGBA")
            left = max(0, bounds["x"])
            top = max(0, bounds["y"])
            right = min(image.width, left + max(0, bounds["w"]))
            bottom = min(image.height, top + max(0, bounds["h"]))
            return image.crop((left, top, right, bottom))

        def mask(source: Image.Image) -> Image.Image:
            masked = Image.new("RGBA", source.size, (0, 0, 0, 255))
            source_pixels = source.load()
            masked_pixels = masked.load()
            for y in range(source.height):
                for x in range(source.width):
                    red, green, blue, alpha = source_pixels[x, y]
                    if alpha > 0 and green >= 35 and green > red * 1.25 and green > blue * 1.1:
                        masked_pixels[x, y] = (0, green, 0, 255)
            return masked

        def labelled(image: Image.Image, text: str) -> Image.Image:
            canvas = Image.new("RGBA", (image.width, image.height + 28), (22, 22, 22, 255))
            canvas.paste(image, (0, 28))
            ImageDraw.Draw(canvas).text((8, 7), text, fill=(235, 235, 235, 255))
            return canvas

        sender_crop = crop(sender_screenshot, sender_bounds)
        receiver_crop = crop(receiver_screenshot, receiver_bounds)
        safe_label = "".join(ch if ch.isalnum() else "_" for ch in label)

        def combine(left: Image.Image, right: Image.Image, filename: str) -> str:
            left = labelled(left, "sender")
            right = labelled(right, "receiver")
            output = Image.new("RGBA", (left.width + right.width + 16, max(left.height, right.height)), (30, 30, 30, 255))
            output.paste(left, (0, 0))
            output.paste(right, (left.width + 16, 0))
            path = self.artifact_dir / filename
            output.save(path)
            return str(path)

        return {
            "cropComparison": combine(sender_crop, receiver_crop, f"{safe_label}_visualiser_crops.png"),
            "greenMaskComparison": combine(mask(sender_crop), mask(receiver_crop), f"{safe_label}_green_masks.png"),
        }

    def require_visual_similarity(
        self,
        label: str,
        sender_snapshot: dict,
        sender_screenshot: Path,
        receiver_snapshot: dict,
        receiver_screenshot: Path,
    ) -> None:
        sender_metrics = self.green_image_metrics(sender_screenshot, self.visualiser_bounds(sender_snapshot))
        receiver_metrics = self.green_image_metrics(receiver_screenshot, self.visualiser_bounds(receiver_snapshot))
        diagnostic_images = self.write_visual_diagnostic_images(
            label,
            sender_screenshot,
            self.visualiser_bounds(sender_snapshot),
            receiver_screenshot,
            self.visualiser_bounds(receiver_snapshot),
        )

        sender_pixels = int(sender_metrics["greenPixels"])
        receiver_pixels = int(receiver_metrics["greenPixels"])
        metrics: dict[str, object] = {
            "sender": sender_metrics,
            "receiver": receiver_metrics,
            "diagnosticImages": diagnostic_images,
            "passed": False,
        }
        failure_reasons: list[str] = []

        if sender_pixels > 0 and receiver_pixels > 0 and "greenBounds" in sender_metrics and "greenBounds" in receiver_metrics:
            sender_bounds = list(sender_metrics["greenBounds"])
            receiver_bounds = list(receiver_metrics["greenBounds"])
            width_ratio = min(int(sender_metrics["greenWidth"]), int(receiver_metrics["greenWidth"])) / max(1, max(int(sender_metrics["greenWidth"]), int(receiver_metrics["greenWidth"])))
            height_ratio = min(int(sender_metrics["greenHeight"]), int(receiver_metrics["greenHeight"])) / max(1, max(int(sender_metrics["greenHeight"]), int(receiver_metrics["greenHeight"])))
            pixel_ratio = min(sender_pixels, receiver_pixels) / max(1, max(sender_pixels, receiver_pixels))
            density_ratio = min(float(sender_metrics["greenBoundsDensity"]), float(receiver_metrics["greenBoundsDensity"])) / max(0.0001, max(float(sender_metrics["greenBoundsDensity"]), float(receiver_metrics["greenBoundsDensity"])))
            strong_pixel_ratio = min(int(sender_metrics["strongGreenPixels"]), int(receiver_metrics["strongGreenPixels"])) / max(1, max(int(sender_metrics["strongGreenPixels"]), int(receiver_metrics["strongGreenPixels"])))
            iou = self.bbox_iou(sender_bounds, receiver_bounds)
            metrics.update(
                {
                    "bboxIou": iou,
                    "widthRatio": width_ratio,
                    "heightRatio": height_ratio,
                    "pixelRatio": pixel_ratio,
                    "densityRatio": density_ratio,
                    "strongPixelRatio": strong_pixel_ratio,
                }
            )
            checks = {
                "senderPixels>=1000": sender_pixels >= 1000,
                "receiverPixels>=1000": receiver_pixels >= 1000,
                "widthRatio>=0.55": width_ratio >= 0.55,
                "heightRatio>=0.55": height_ratio >= 0.55,
                "pixelRatio>=0.45": pixel_ratio >= 0.45,
                "densityRatio>=0.45": density_ratio >= 0.45,
                "bboxIou>=0.20": iou >= 0.20,
            }
            failure_reasons = [name for name, passed in checks.items() if not passed]
            metrics["passed"] = not failure_reasons
        else:
            if sender_pixels <= 0:
                failure_reasons.append("sender had no green pixels")
            if receiver_pixels <= 0:
                failure_reasons.append("receiver had no green pixels")

        metrics["failureReasons"] = failure_reasons
        self.summary[label] = metrics
        if not metrics["passed"]:
            raise StepError(f"{label} did not show similar sender/receiver visualiser output; metrics={metrics}")

    def select_texture_input_source(self) -> None:
        self.run_session_command(
            self.receiver,
            "receiver select texture input",
            "select-option",
            "--role",
            "menuBar",
            "--class",
            "MenuBarComponent",
            "--text",
            "Select Texture Input...",
            "--timeout-ms",
            "7000",
            "--menu-item",
            self.args.source_prefix,
            retries=8,
            retry_delay=1.0,
        )

    def prepare(self) -> None:
        if self.transport == "syphon" and not is_macos():
            raise SystemExit("Syphon E2E requires macOS.")
        if self.transport == "spout" and not is_windows():
            raise SystemExit("Spout E2E requires Windows.")

        if not self.sender.find_jucewright():
            self.sender.build_jucewright()
            if not self.sender.find_jucewright():
                raise SystemExit("Could not find jucewright after building it.")

        self.receiver.jucewright = self.sender.jucewright
        if self.sender.build_app_requested or not is_executable(self.sender.app_executable):
            self.sender.build_app()

        if not is_executable(self.sender.app_executable):
            raise SystemExit(f"App executable not found: {self.sender.app_executable}")
        if self.sender.jucewright is None or not is_executable(self.sender.jucewright):
            raise SystemExit(f"jucewright not executable: {self.sender.jucewright}")

        self.summary["jucewright"] = str(self.sender.jucewright)
        self.summary["appExecutable"] = str(self.sender.app_executable)
        self.log(f"Artifacts: {self.artifact_dir}")
        self.log(f"jucewright: {self.sender.jucewright}")
        self.log(f"App: {self.sender.app_executable}")

    def launch_app(self, session: BrowserSession, label: str) -> None:
        if is_macos():
            session.launch_app(label)
            return

        session.stop_app()
        time.sleep(0.5)
        session.launch_index += 1
        suffix = f"{session.launch_index:02d}_{slug(label)}"
        launch_home = session.automation_home_root / suffix
        launch_home.mkdir(parents=True, exist_ok=True)
        launch_log = session.artifact_dir / f"launch.{suffix}.json"
        stdout_log = session.artifact_dir / f"app.{suffix}.stdout.log"
        stderr_log = session.artifact_dir / f"app.{suffix}.stderr.log"
        command = session.jw(
            "launch",
            "--app",
            session.app_path,
            "--session",
            session.session,
            "--artifact-dir",
            session.artifact_dir,
            "--home",
            launch_home,
            "--stdout",
            stdout_log,
            "--stderr",
            stderr_log,
            "--timeout-ms",
            session.session_timeout_seconds * 1000,
        )
        output = self.run_command(f"launch {label}", [str(part) for part in command])
        launch_log.write_text(output, encoding="utf-8")

        try:
            payload = json.loads(output)
            matched = payload.get("matchedSession", {})
            pid = matched.get("pid")
            session.app_pid = int(pid) if pid is not None else None
        except Exception:
            session.app_pid = None

        time.sleep(0.9)

    def run(self) -> int:
        try:
            self.prepare()
            fixture = self.write_wav_fixture()

            self.launch_app(self.sender, "sender")
            self.run_session_command(self.sender, "sender wait startup", "wait", "--ms", "700")
            self.run_session_command(self.sender, "sender drop wav fixture", "drop-files", "--file", fixture, "--timeout-ms", "7000")
            self.run_session_command(self.sender, "sender wait after drop", "wait", "--ms", "1200")
            sender_before_output = self.snapshot(self.sender, "sender snapshot before output", "sender_before_output.json")
            sender_before_output_screenshot = self.screenshot(self.sender, "sender visual before output", "sender_before_output.png")
            self.run_session_command(self.sender, "sender enable texture output", "click", "--name", "textureOutput", "--exact", "--timeout-ms", "7000")
            self.run_session_command(self.sender, "sender wait for texture output", "wait", "--ms", "2200")
            sender_after_output = self.snapshot(self.sender, "sender snapshot after output", "sender_after_output.json")
            sender_after_output_screenshot = self.screenshot(self.sender, "sender visual after output", "sender_after_output.png")
            publish_failed = self.has_publish_failed_alert(sender_after_output)
            self.summary.update(
                {
                    "senderAfterOutputScreenshot": str(sender_after_output_screenshot),
                    "senderPublishFailedAlert": publish_failed,
                }
            )
            if publish_failed:
                raise StepError("sender showed Texture Output / Publish failed after enabling texture output")

            self.launch_app(self.receiver, "receiver")
            self.run_session_command(self.receiver, "receiver wait startup", "wait", "--ms", "900")
            self.select_texture_input_source()
            self.run_session_command(self.receiver, "receiver wait after input selection", "wait", "--ms", "1800")

            before = self.snapshot(self.receiver, "receiver snapshot before settings", "receiver_before_settings.json")
            self.require_texture_input_label(before, "receiverLabelsBeforeSettings")
            receiver_before_settings = self.screenshot(self.receiver, "receiver screenshot before settings", "receiver_before_settings.png")

            self.require_visual_similarity(
                "beforeSettingsVisualSimilarity",
                sender_before_output,
                sender_before_output_screenshot,
                before,
                receiver_before_settings,
            )

            sender_overlay_open = True
            sender_overlay_closed = True
            during = before
            after_close = before
            receiver_while_settings_open = None
            receiver_after_close = receiver_before_settings

            if not self.args.skip_settings_reconnect:
                self.run_session_command(
                    self.sender,
                    "sender open recording settings",
                    "select-option",
                    "--role",
                    "menuBar",
                    "--class",
                    "MenuBarComponent",
                    "--text",
                    "Recording Settings...",
                    "--timeout-ms",
                    "7000",
                )
                self.run_session_command(self.sender, "sender wait with recording settings open", "wait", "--ms", "2200")
                sender_open = self.snapshot(self.sender, "sender snapshot with settings open", "sender_settings_open.json")
                during = self.snapshot(self.receiver, "receiver snapshot while settings open", "receiver_while_settings_open.json")
                receiver_while_settings_open = self.screenshot(self.receiver, "receiver screenshot while settings open", "receiver_while_settings_open.png")

                self.run_session_command(self.sender, "sender close recording settings", "click", "--role", "button", "--name", "closeOverlay", "--exact", "--timeout-ms", "7000")
                self.run_session_command(self.sender, "sender wait after closing settings", "wait", "--ms", "2200")
                sender_after_close = self.snapshot(self.sender, "sender snapshot after closing settings", "sender_after_close.json")
                after_close = self.snapshot(self.receiver, "receiver snapshot after closing settings", "receiver_after_close.json")
                self.require_texture_input_label(after_close, "receiverLabelsAfterClose")
                receiver_after_close = self.screenshot(self.receiver, "receiver screenshot after closing settings", "receiver_after_close.png")
                self.require_visual_similarity(
                    "afterCloseVisualSimilarity",
                    sender_before_output,
                    sender_before_output_screenshot,
                    after_close,
                    receiver_after_close,
                )

                sender_overlay_open = self.snapshot_contains(sender_open, "Recording Settings")
                sender_overlay_closed = not self.snapshot_contains(sender_after_close, "Recording Settings")

            receiver_after_source_removed = None
            if not self.args.skip_source_removal:
                self.run_session_command(self.sender, "sender disable texture output", "click", "--name", "textureOutput", "--exact", "--timeout-ms", "7000")
                self.run_session_command(self.receiver, "receiver wait after sender disabled output", "wait", "--ms", "1800")
                receiver_after_source_removed = self.snapshot(self.receiver, "receiver snapshot after source removed", "receiver_after_source_removed.json")
                self.require_source_removed_state(receiver_after_source_removed, "receiverAfterSourceRemoved")

            texture_label_before = self.has_texture_input_label(before)
            texture_label_during = self.has_texture_input_label(during)
            texture_label_after_close = self.has_texture_input_label(after_close)
            alert_during = self.has_connection_lost_alert(during)
            alert_after_close = self.has_connection_lost_alert(after_close)

            self.summary.update(
                {
                    "senderBeforeOutputScreenshot": str(sender_before_output_screenshot) if sender_before_output_screenshot else None,
                    "receiverBeforeSettingsScreenshot": str(receiver_before_settings),
                    "receiverWhileSettingsOpenScreenshot": str(receiver_while_settings_open) if receiver_while_settings_open else None,
                    "receiverAfterCloseScreenshot": str(receiver_after_close),
                    "receiverAfterSourceRemovedSnapshot": str(self.artifact_dir / "receiver_after_source_removed.json") if receiver_after_source_removed is not None else None,
                    "textureLabelBefore": texture_label_before,
                    "textureLabelWhileSettingsOpen": texture_label_during,
                    "textureLabelAfterClose": texture_label_after_close,
                    "connectionLostWhileSettingsOpen": alert_during,
                    "connectionLostAfterClose": alert_after_close,
                    "senderOverlayOpen": sender_overlay_open,
                    "senderOverlayClosed": sender_overlay_closed,
                }
            )

            failed = (
                not texture_label_before
                or not texture_label_during
                or not texture_label_after_close
                or alert_during
                or alert_after_close
                or not sender_overlay_open
                or not sender_overlay_closed
            )
            self.summary["passed"] = not failed
            return 1 if failed else 0
        except Exception as exc:
            self.summary["error"] = str(exc)
            self.summary["traceback"] = traceback.format_exc()
            self.summary["passed"] = False
            return 1
        finally:
            self.sender.stop_app()
            self.receiver.stop_app()
            summary_file = self.artifact_dir / "e2e_summary.json"
            summary_file.write_text(json.dumps(self.summary, indent=2) + "\n", encoding="utf-8")
            self.log(f"Summary: {summary_file}")


def main() -> int:
    return TextureSharingE2ERun(parse_args()).run()


if __name__ == "__main__":
    sys.exit(main())
