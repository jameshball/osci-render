#!/usr/bin/env python3
"""Compile and run Motion's independent C++20 model, render and editor math tests."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cxx", default=os.environ.get("CXX"), help="C++20 compiler command; defaults to clang++ or g++")
parser.add_argument("--sanitizer", choices=("address", "thread"), help="Address includes undefined-behaviour checks")
parser.add_argument("--filter", default="", help="Case-insensitive test filename substring")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
compiler = shlex.split(args.cxx) if args.cxx else [shutil.which("clang++") or shutil.which("g++") or ""]
if not compiler or not compiler[0]:
    parser.error("A C++20 compiler is required; set CXX or pass --cxx.")
tests = sorted(path for path in (root / "tests/motion").glob("*Tests.cpp") if args.filter.lower() in path.name.lower())
if not tests:
    parser.error("No tests matched --filter.")
flags = ["-std=c++20", "-Wall", "-Wextra", "-pthread", "-O1", "-g"]
if args.sanitizer:
    flags += ["-fno-omit-frame-pointer", "-fsanitize=" + ("address,undefined" if args.sanitizer == "address" else "thread")]
with tempfile.TemporaryDirectory(prefix="motion-portable-tests-") as directory:
    for source in tests:
        executable = Path(directory) / (source.stem + (".exe" if os.name == "nt" else ""))
        print(f"Building and running {source.stem}", flush=True)
        subprocess.run(compiler + flags + [str(source), "-o", str(executable)], cwd=root, check=True, timeout=180)
        subprocess.run([str(executable)], cwd=root, check=True, timeout=180)
print(f"All {len(tests)} Motion portable test executables passed.", flush=True)
