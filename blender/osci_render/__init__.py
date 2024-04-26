bl_info = {
    "name": "osci-render",
    "author": "James Ball", 
    "version": (1, 0, 2),
    "blender": (3, 1, 2),
    "location": "View3D",
    "description": "Addon to send gpencil frames over to osci-render",
    "warning": "Requires a camera and gpencil object",
    "wiki_url": "https://github.com/jameshball/osci-render",
    "category": "Development",
}

import bpy
import os
import bmesh
import socket
import json
import atexit
from bpy.props import StringProperty
from bpy.app.handlers import persistent
from bpy_extras.io_utils import ImportHelper

HOST = "localhost"
PORT = 51677

sock = None


class OBJECT_PT_osci_render_settings(bpy.types.Panel):
    bl_idname = "OBJECT_PT_osci_render_settings"
    bl_label = "osci-render settings"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "render"

    def draw_header(self, context):
        layout = self.layout

    def draw(self, context):
        global sock
        if sock is None:
            self.layout.operator("render.osci_render_connect", text="Connect to osci-render instance")
        else:
            self.layout.operator("render.osci_render_close", text="Close osci-render connection")
        self.layout.operator("render.osci_render_save", text="Save line art to file")

class osci_render_connect(bpy.types.Operator):
    bl_label = "Connect to osci-render"
    bl_idname = "render.osci_render_connect"
    bl_description = "Connect to osci-render"

    def execute(self, context):
        global sock
        if sock is None:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(1)
                sock.connect((HOST, PORT))
                send_scene_to_osci_render(bpy.context.scene)
            except socket.error as exp:
                sock = None
                self.report({"WARNING"}, "Failed to connect to osci-render - make sure it is running first!")
                return {"CANCELLED"}
                
        return {"FINISHED"}
        
class osci_render_save(bpy.types.Operator, ImportHelper):
    bl_label = "Save Line Art"
    bl_idname = "render.osci_render_save"
    bl_description = "Save line art to the chosen file"
    filename_ext = ".gpla"

    filter_glob: StringProperty(
        default="*.gpla",
        options={"HIDDEN"}
    )
    
    def execute(self, context):
        FilePath = self.filepath
        filename, extension = os.path.splitext(self.filepath)

        if extension != ".gpla":
            extension = ".gpla"
            FilePath = FilePath + ".gpla"
        
        self.report({"INFO"}, FilePath)

        if filename is not None and extension is not None:
            fin = save_scene_to_file(bpy.context.scene, FilePath)
            if fin == 0:
                self.report({"INFO"}, "File write successful!")
                return {"FINISHED"}
            else:
                self.report({"WARNING"}, "Something went wrong in saving the file")
        else:
            filename = None
            extension = None
            self.report({"WARNING"}, "The filename or extension isn't right, action stopped for your own safety")
            return {"CANCELLED"}


class osci_render_close(bpy.types.Operator):
    bl_label = "Close osci-render connection"
    bl_idname = "render.osci_render_close"

    def execute(self, context):
        close_osci_render()
        
        return {"FINISHED"}


@persistent
def close_osci_render():
    global sock
    if sock is not None:
        try:
            sock.send("CLOSE\n".encode('utf-8'))
            sock.close()
        except socket.error as exp:
            sock = None


def append_matrix(object_info, obj):
    camera_space = bpy.context.scene.camera.matrix_world.inverted() @ obj.matrix_world
    object_info["matrix"] = [camera_space[i][j] for i in range(4) for j in range(4)]
    return object_info

def get_frame_info():
    frame_info = {"objects": []}
    
    for obj in bpy.data.objects:
        if obj.visible_get() and obj.type == 'GPENCIL':
            object_info = {"name": obj.name}
            strokes = obj.data.layers.active.frames.data.active_frame.strokes
            object_info["vertices"] = []
            for stroke in strokes:
                object_info["vertices"].append([{
                    "x": vert.co[0],
                    "y": vert.co[1],
                    "z": vert.co[2],
                } for vert in stroke.points])
            
            frame_info["objects"].append(append_matrix(object_info, obj))
    
    frame_info["focalLength"] = -0.05 * bpy.data.cameras[0].lens

    return frame_info
    
@persistent
def save_scene_to_file(scene, file_path):
    return_frame = scene.frame_current
    
    scene_info = {"frames": []}
    for frame in range(0, scene.frame_end - scene.frame_start):
        scene.frame_set(frame + scene.frame_start)
        scene_info["frames"].append(get_frame_info())

    json_str = json.dumps(scene_info, separators=(',', ':'))
    
    if file_path is not None:
        f = open(file_path, "w")
        f.write(json_str)
        f.close()
    else:
        return 1
        
    scene.frame_set(return_frame)
    return 0


@persistent
def send_scene_to_osci_render(scene):
    global sock

    if sock is not None:
        frame_info = get_frame_info()

        json_str = json.dumps(frame_info, separators=(',', ':')) + '\n'
        try:
            print(json_str)
            sock.sendall(json_str.encode('utf-8'))
        except socket.error as exp:
            sock = None


operations = [OBJECT_PT_osci_render_settings, osci_render_connect, osci_render_close, osci_render_save]


def register():
    bpy.app.handlers.frame_change_pre.append(send_scene_to_osci_render)
    bpy.app.handlers.depsgraph_update_post.append(send_scene_to_osci_render)
    atexit.register(close_osci_render)
    for operation in operations:
        bpy.utils.register_class(operation)


def unregister():
    bpy.app.handlers.frame_change_pre.remove(send_scene_to_osci_render)
    bpy.app.handlers.depsgraph_update_post.remove(send_scene_to_osci_render)
    atexit.unregister(close_osci_render)
    for operation in reversed(operations):
        bpy.utils.unregister_class(operation)


if __name__ == "__main__":
    register()
