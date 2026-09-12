from __future__ import annotations

import argparse
from pathlib import Path

from .scenario import OsciRenderBrowserRun


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Browse osci-render with Jucewright.")
    parser.add_argument("--build-app", action="store_true", help="Resave the Projucer project and build the Debug standalone app.")
    parser.add_argument("--quick", action="store_true", help="Run a shorter smoke pass instead of the exhaustive pass.")
    parser.add_argument("--legal-only", action="store_true", help="Exercise the first-time legal overlay in the isolated profile, then stop.")
    parser.add_argument("--installer-legal-only", action="store_true", help="Exercise installer Privacy & Terms without starting an installation; pass --app.")
    parser.add_argument("--window-width", type=int, help="Resize the app window before running the scenario.")
    parser.add_argument("--window-height", type=int, help="Resize the app window before running the scenario.")
    parser.add_argument("--keep-app", action="store_true", help="Leave the launched osci-render process running.")
    parser.add_argument("--native-dialogs", action="store_true", help="Include actions that may open native file dialogs.")
    parser.add_argument("--artifact-dir", help="Write logs, JSON snapshots, screenshots, and traces to this directory.")
    parser.add_argument("--jucewright", help="Use a specific jucewright executable.")
    parser.add_argument("--app", "--app-path", "--app-bundle", dest="app_path", help="Use a specific app bundle or standalone executable.")
    parser.add_argument("--app-executable", help="Executable path used only for preflight existence checks.")
    parser.add_argument("--audio-output", help="Use this audio output in the isolated automation profile.")
    parser.add_argument("--session", help="Jucewright session name.")
    args = parser.parse_args()
    if args.installer_legal_only:
        if not args.app_path:
            parser.error('--installer-legal-only requires --app')
        args.legal_only = True
        if not args.app_executable:
            app = Path(args.app_path)
            args.app_executable = str(app / 'Contents/MacOS' / app.stem if app.suffix == '.app' else app)
    return args


def main() -> int:
    run = OsciRenderBrowserRun(parse_args())
    return run.run()


if __name__ == "__main__":
    sys.exit(main())
