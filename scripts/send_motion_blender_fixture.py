"""Run inside factory-startup Blender to exercise its real add-on TCP sender."""
import importlib.util
from pathlib import Path
import sys
import bpy

addon_path, port = sys.argv[sys.argv.index('--') + 1:]
spec = importlib.util.spec_from_file_location('osci_motion_live_fixture_addon', addon_path)
addon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(addon)
addon.register()
try:
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.object.camera_add(location=(0, 0, 5), rotation=(0, 0, 0))
    scene = bpy.context.scene
    scene.camera = bpy.context.object
    scene.camera.data.lens = 50
    scene.render.fps = 30
    modern = bpy.app.version >= (4, 3, 0)
    if modern:
        bpy.ops.object.grease_pencil_add(type='EMPTY')
    else:
        bpy.ops.object.gpencil_add(type='EMPTY')
    obj = bpy.context.object
    layer = obj.data.layers.new('Motion input test', set_active=True)
    frame = layer.frames.new(1)
    coordinates = [(0, 0, 0), (1, 0, 0), (.5, 1, 0)]
    if modern:
        frame.drawing.add_strokes([3])
        stroke = frame.drawing.strokes[0]
        for point, coordinate in zip(stroke.points, coordinates):
            point.position = coordinate
        stroke.cyclic = True
    else:
        stroke = frame.strokes.new()
        stroke.points.add(count=3)
        for point, coordinate in zip(stroke.points, coordinates):
            point.co = coordinate
        stroke.use_cyclic = True
    bpy.context.view_layer.update()
    scene.oscirenderPort = int(port)
    assert bpy.ops.render.osci_render_connect() == {'FINISHED'}
    obj.location.x = 2
    bpy.context.view_layer.update()
    addon.send_scene_to_osci_render(scene)
    assert bpy.ops.render.osci_render_close() == {'FINISHED'}
    print('Motion Blender fixture sent using actual connect/update/close operators', flush=True)
finally:
    addon.close_osci_render()
    addon.unregister()
