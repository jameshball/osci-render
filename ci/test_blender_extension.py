"""Exercise repository setup, persistence, migration and failure recovery in Blender."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blender", required=True)
    parser.add_argument("--repository", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    helper = root / "Source/installer/blender_setup.py"
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(SimpleHTTPRequestHandler, directory=str(args.repository.resolve())))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix="osci-blender-test-") as temporary:
            directory = Path(temporary)
            env = {**os.environ, "BLENDER_USER_RESOURCES": str(directory / "profile")}
            url = f"http://127.0.0.1:{server.server_port}/index.json"
            base = [args.blender, "--background", "--disable-autoexec", "--python-exit-code", "1"]

            def run(extra, success=True):
                result = subprocess.run(base + extra, env=env, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, timeout=120)
                if (result.returncode == 0) != success:
                    raise AssertionError(f"Blender exited {result.returncode}:\n{result.stdout}")
                return result.stdout

            def action(name, *extra, success=True, repository_url=url):
                online = ["--online-mode"] if name == "install" else []
                output = run(online + ["--python", str(helper), "--", name, "--url", repository_url,
                             "--journal", str(directory / "journal.json"), *extra], success=success)
                lines = [line[12:] for line in output.splitlines() if line.startswith("OSCI_RESULT=")]
                assert lines, output
                state = json.loads(lines[-1])
                assert state["ok"] == success, output
                return state

            initial = action("inspect")
            assert not initial["enabled"]
            action("prepare")
            action("install")
            assert action("verify")["enabled"]
            # Repeat setup to test existing repository reuse and activation persistence.
            action("prepare")
            action("install")
            assert action("verify")["enabled"]
            run(["--python", str(root / "ci/blender_extension_smoke.py")])
            # An existing local/legacy selection requires explicit migration.
            run(["--python-expr", "import bpy; bpy.context.preferences.addons.new().module = 'osci_render'; bpy.ops.wm.save_userpref()"])
            assert "osci_render" in action("inspect")["conflicts"]
            action("prepare", success=False)
            action("prepare", "--replace")
            assert not action("inspect")["conflicts"]
            action("rollback")
            assert "osci_render" in action("inspect")["conflicts"]
            action("prepare", "--replace")
            action("install")
            assert action("verify")["enabled"]
            # Failed downloads are reported instead of claiming installation succeeded.
            action("prepare")
            server.shutdown()
            server.server_close()
            action("install", success=False)
            action("rollback")
            assert action("verify")["enabled"]
            print(f"PASS: Blender {initial['version']} repository installation, repair, migration, rollback and extension smoke tests")
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    main()
