"""Executed in an isolated Blender profile after repository installation."""

import importlib
import socket
import tempfile
from pathlib import Path

import bpy

extension = importlib.import_module("bl_ext.osci_render.osci_render")
assert hasattr(bpy.types.Scene, "oscirenderPort")
extension.unregister()
extension.unregister()
assert not hasattr(bpy.types.Scene, "oscirenderPort")
extension.register()
extension.register()
assert bpy.app.handlers.frame_change_pre.count(extension.send_scene_to_osci_render) == 1
assert bpy.app.handlers.depsgraph_update_post.count(extension.send_scene_to_osci_render) == 1

scene = bpy.context.scene
assert scene.camera is not None
scene.frame_start = scene.frame_end = 1
if bpy.app.version >= (4, 3, 0):
    bpy.ops.object.grease_pencil_add(type='EMPTY')
    layer = bpy.context.object.data.layers.new("Empty test layer", set_active=True)
    # Empty layers must export without an AttributeError.
    assert extension.get_frame_info_binary().startswith(b"FRAME   ")
    drawing = layer.frames.new(1).drawing
    drawing.add_strokes([2])
    drawing.strokes[0].points[0].position = (0, 0, 0)
    drawing.strokes[0].points[1].position = (1, 1, 0)
else:
    bpy.ops.object.gpencil_add(type='EMPTY')
    layer = bpy.context.object.data.layers.new("Empty test layer", set_active=True)
    assert extension.get_frame_info_binary().startswith(b"FRAME   ")
    stroke = layer.frames.new(1).strokes.new()
    stroke.points.add(count=2)
    stroke.points[0].co = (0, 0, 0)
    stroke.points[1].co = (1, 1, 0)
bpy.context.view_layer.update()
assert b"STROKE  " in extension.get_frame_info_binary()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "test.gpla"
    assert extension.save_scene_to_file(scene, str(path)) == 0
    assert path.read_bytes().startswith(b"GPLA    ")
    assert path.read_bytes().endswith(b"END GPLA")
camera = scene.camera
scene.camera = None
try:
    extension.get_frame_info_binary()
    raise AssertionError("Missing camera should be reported")
except ValueError:
    pass
scene.camera = camera
extension.sock = socket.socket()
extension.close_osci_render()
assert extension.sock is None
extension.unregister()
print("PASS: registration, empty layers, stroke export, GPLA export, camera validation and disconnect")
