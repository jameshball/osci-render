from __future__ import annotations

import os
import platform
import tempfile
from pathlib import Path


def is_windows() -> bool:
    return platform.system().lower() == "windows"


def is_macos() -> bool:
    return platform.system().lower() == "darwin"


def is_linux() -> bool:
    return platform.system().lower() == "linux"


def executable_name(name: str) -> str:
    return f"{name}.exe" if is_windows() else name


def user_cache_root() -> Path:
    if is_macos():
        return Path.home() / "Library" / "Caches"
    if is_windows():
        return Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))


def default_build_dir() -> Path:
    return Path(tempfile.gettempdir()) / "jucewright-osci-render-cli"


def default_app_path(root: Path) -> Path:
    if is_macos():
        return root / "Builds" / "osci-render" / "MacOSX" / "build" / "Debug" / "osci-render.app"
    if is_windows():
        return root / "Builds" / "osci-render" / "VisualStudio2022" / "x64" / "Debug" / "Standalone Plugin" / "osci-render.exe"
    return root / "Builds" / "osci-render" / "LinuxMakefile" / "build" / "osci-render"


def default_app_executable(app_path: Path) -> Path:
    if is_macos() and app_path.suffix == ".app":
        return app_path / "Contents" / "MacOS" / "osci-render"
    return app_path


def is_executable(path: Path) -> bool:
    if not path.is_file():
        return False
    return is_windows() or os.access(path, os.X_OK)


def process_exists(pid: int) -> bool:
    if pid <= 0:
        return False

    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
