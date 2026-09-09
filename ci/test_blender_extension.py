"""Exercise repository setup, persistence, migration and failure recovery in Blender."""

import sys

sys.dont_write_bytecode = True

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
    class RepositoryHandler(SimpleHTTPRequestHandler):
        corrupt_downloads = False

        def do_GET(self):
            if self.corrupt_downloads and self.path.endswith(".zip"):
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"corrupt extension archive")
            else:
                super().do_GET()

    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(RepositoryHandler, directory=str(args.repository.resolve())))
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

            # Setup must preserve offline mode and unrelated preferences.
            run(["--python-expr", "import bpy; bpy.context.preferences.system.use_online_access = False; bpy.context.preferences.view.show_splash = False; bpy.ops.wm.save_userpref()"])
            initial = action("inspect")
            assert not initial["enabled"]
            # A first installation with a corrupt download must stay disabled after rollback.
            RepositoryHandler.corrupt_downloads = True
            action("prepare")
            action("install", success=False)
            action("rollback")
            assert not action("inspect")["enabled"]
            RepositoryHandler.corrupt_downloads = False
            action("prepare")
            action("install")
            assert action("verify")["enabled"]
            # Repeat setup to test existing repository reuse and activation persistence.
            action("prepare")
            action("install")
            assert action("verify")["enabled"]
            run(["--python", str(Path(__file__).resolve()), "--", "--smoke"])
            # Rollback must restore an enabled target even if a failed update removed it.
            action("prepare")
            run(["--python-expr", "import bpy; addons = bpy.context.preferences.addons; addons.remove(addons['bl_ext.osci_render.osci_render']); bpy.ops.wm.save_userpref()"])
            action("rollback")
            assert action("verify")["enabled"]
            # An existing local/legacy selection requires explicit migration.
            legacy = directory / "profile/scripts/addons/osci_render.py"
            legacy.parent.mkdir(parents=True, exist_ok=True)
            legacy.write_text("bl_info = {'name': 'osci-render legacy', 'blender': (4, 2, 0)}\ndef register(): pass\ndef unregister(): raise RuntimeError('broken legacy unregister')\n")
            run(["--python-expr", "import bpy, addon_utils; addon_utils.enable('osci_render', default_set=True); bpy.ops.wm.save_userpref()"])
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
            run(["--python-expr", "import bpy; assert not bpy.context.preferences.system.use_online_access; assert not bpy.context.preferences.view.show_splash"])
            print(f"PASS: Blender {initial['version']} repository installation, repair, migration, rollback and extension smoke tests")
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def smoke_test():
    import base64
    import importlib
    import socket
    import struct
    import bpy

    extension = importlib.import_module("bl_ext.osci_render.osci_render")
    extension.unregister()
    extension.unregister()
    assert not hasattr(bpy.types.Scene, "oscirenderPort")
    extension.register()
    extension.register()
    assert bpy.app.handlers.frame_change_post.count(extension.send_scene_to_osci_render) == 1
    assert bpy.app.handlers.depsgraph_update_post.count(extension.send_scene_to_osci_render) == 1
    scene = bpy.context.scene
    modern = bpy.app.version >= (4, 3, 0)
    if modern:
        bpy.ops.object.grease_pencil_add(type='EMPTY')
    else:
        bpy.ops.object.gpencil_add(type='EMPTY')
    obj = bpy.context.object
    layer = obj.data.layers.new("Test layer", set_active=True)
    assert b"STROKE  " not in extension.get_frame_info_binary()
    if modern:
        drawing = layer.frames.new(1).drawing
        drawing.add_strokes([2])
        drawing.strokes[0].points[0].position = (0, 0, 0)
        drawing.strokes[0].points[1].position = (1, 1, 0)
        drawing.strokes[0].cyclic = True
    else:
        stroke = layer.frames.new(1).strokes.new()
        stroke.points.add(count=2)
        stroke.points[0].co = (0, 0, 0)
        stroke.points[1].co = (1, 1, 0)
        stroke.use_cyclic = True
    bpy.context.view_layer.update()
    data = extension.get_frame_info_binary()
    index = data.index(b"vertexCt") + 8
    assert struct.unpack_from("<Q", data, index)[0] == 3
    index = data.index(b"VERTICES") + 8
    assert struct.unpack_from("<9d", data, index) == (0, 0, 0, 1, 1, 0, 0, 0, 0)
    layer.hide = True
    bpy.context.view_layer.update()
    assert b"STROKE  " not in extension.get_frame_info_binary()
    layer.hide = False
    bpy.context.view_layer.update()
    # The selected scene camera, not the first camera datablock, supplies the lens.
    camera = scene.camera
    other_camera = camera.copy()
    other_camera.data = camera.data.copy()
    scene.collection.objects.link(other_camera)
    other_camera.data.lens = 85
    scene.camera = other_camera
    bpy.context.view_layer.update()
    assert struct.unpack_from("<d", extension.get_frame_info_binary(), 16)[0] == -4.25
    scene.camera = camera
    scene.frame_start, scene.frame_end = 1, 3
    scene.frame_set(2, subframe=0.25)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "test.gpla"
        extension.save_scene_to_file(scene, str(path))
        data = path.read_bytes()
        assert data.startswith(b"GPLA    ") and data.endswith(b"END GPLA")
        assert struct.unpack_from("<Q", data, 48)[0] == 3
        assert data.count(b"FRAME   ") == 3
        assert scene.frame_current == 2 and scene.frame_subframe == 0.25
        try:
            extension.save_scene_to_file(scene, str(Path(directory) / "missing/test.gpla"))
            raise AssertionError("Unwritable path should fail")
        except OSError:
            pass
        assert scene.frame_current == 2 and scene.frame_subframe == 0.25
    # A real TCP listener verifies Blender's connect operator and wire framing.
    with socket.socket() as listener:
        for port in range(51600, 51700):
            try:
                listener.bind(("127.0.0.1", port))
                break
            except OSError:
                continue
        else:
            raise AssertionError("No test port available")
        listener.listen()
        listener.settimeout(5)
        scene.oscirenderPort = port
        assert bpy.ops.render.osci_render_connect() == {'FINISHED'}
        connection, _ = listener.accept()
        with connection:
            connection.settimeout(5)
            with connection.makefile('rb') as stream:
                data = base64.b64decode(stream.readline().strip(), validate=True)
                assert data == extension.get_gpla_file(scene)
                assert struct.unpack_from("<Q", data, 48)[0] == 1
                assert data.count(b"FRAME   ") == 1
                assert bpy.ops.render.osci_render_close() == {'FINISHED'}
                assert stream.readline() == b"CLOSE\n"
        assert extension.sock is None
    assert bpy.ops.render.osci_render_connect() == {'CANCELLED'}
    assert extension.sock is None
    scene.camera = None
    assert bpy.ops.render.osci_render_connect() == {'CANCELLED'}
    try:
        extension.get_frame_info_binary()
        raise AssertionError("Missing camera should fail")
    except ValueError:
        pass
    scene.camera = camera
    extension.unregister()
    assert extension.send_scene_to_osci_render not in bpy.app.handlers.frame_change_post
    print("PASS: registration, cyclic strokes, hidden layers, scene camera, animation export, frame restoration and TCP streaming")


if __name__ == "__main__":
    if "--smoke" in sys.argv:
        smoke_test()
    else:
        main()
