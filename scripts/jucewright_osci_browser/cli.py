from __future__ import annotations

import argparse

from .scenario import OsciRenderBrowserRun


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Browse osci-render with Jucewright.")
    parser.add_argument("--build-app", action="store_true", help="Resave the Projucer project and build the Debug standalone app.")
    parser.add_argument("--quick", action="store_true", help="Run a shorter smoke pass instead of the exhaustive pass.")
    parser.add_argument("--feedback-only", action="store_true", help="Exercise only the in-app feedback submission flow.")
    parser.add_argument("--keep-app", action="store_true", help="Leave the launched osci-render process running.")
    parser.add_argument("--native-dialogs", action="store_true", help="Include actions that may open native file dialogs.")
    parser.add_argument("--artifact-dir", help="Write logs, JSON snapshots, screenshots, and traces to this directory.")
    parser.add_argument("--jucewright", help="Use a specific jucewright executable.")
    parser.add_argument("--app", "--app-path", "--app-bundle", dest="app_path", help="Use a specific app bundle or standalone executable.")
    parser.add_argument("--app-executable", help="Executable path used only for preflight existence checks.")
    parser.add_argument("--session", help="Jucewright session name.")
    return parser.parse_args()


def main() -> int:
    run = OsciRenderBrowserRun(parse_args())
    return run.run()


if __name__ == "__main__":
    sys.exit(main())
