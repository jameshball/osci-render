bl_info = {
    "name": "osci-render",
    "author": "James Ball", 
    "version": (1, 1, 0),
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
import struct
import base64
from bpy.props import StringProperty
from bpy.app.handlers import persistent
from bpy_extras.io_utils import ImportHelper

HOST = "localhost"
PORT = 51677

sock = None


GPLA_MAJOR = 2
GPLA_MINOR = 0
GPLA_PATCH = 0


class OBJECT_PT_osci_render_settings(bpy.types.Panel):
    bl_idname = "OBJECT_PT_osci_render_settings"
    bl_label = "DEVELOPMENT osci-render settings"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "render"

    def draw_header(self, context):
        layout = self.layout

    def draw(self, context):
        self.layout.prop(context.scene, "oscirenderPort")
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
                sock.connect((HOST, context.scene.oscirenderPort))
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

def get_gpla_file_allframes(scene):
    bin = bytearray()
    
    # header
    bin.extend(("GPLA    ").encode("utf8"))
    bin.extend(GPLA_MAJOR.to_bytes(8, "little"))
    bin.extend(GPLA_MINOR.to_bytes(8, "little"))
    bin.extend(GPLA_PATCH.to_bytes(8, "little"))
    
    # file info
    bin.extend(("FILE    ").encode("utf8"))
    bin.extend(("fCount  ").encode("utf8"))
    bin.extend((scene.frame_end - scene.frame_start + 1).to_bytes(8, "little"))
    bin.extend(("fRate   ").encode("utf8"))
    bin.extend(scene.render.fps.to_bytes(8, "little"))
    bin.extend(("DONE    ").encode("utf8"))
    
    for frame in range(0, scene.frame_end - scene.frame_start + 1):
        scene.frame_set(frame + scene.frame_start)
        bin.extend(get_frame_info_binary())
    
    bin.extend(("END GPLA").encode("utf8"))
    
    return bin
    
def get_gpla_file(scene):
    bin = bytearray()
    
    # header
    bin.extend(("GPLA    ").encode("utf8"))
    bin.extend(GPLA_MAJOR.to_bytes(8, "little"))
    bin.extend(GPLA_MINOR.to_bytes(8, "little"))
    bin.extend(GPLA_PATCH.to_bytes(8, "little"))
    
    # file info
    bin.extend(("FILE    ").encode("utf8"))
    bin.extend(("fCount  ").encode("utf8"))
    bin.extend((scene.frame_end - scene.frame_start + 1).to_bytes(8, "little"))
    bin.extend(("fRate   ").encode("utf8"))
    bin.extend(scene.render.fps.to_bytes(8, "little"))
    bin.extend(("DONE    ").encode("utf8"))
    
    bin.extend(get_frame_info_binary())
    
    bin.extend(("END GPLA").encode("utf8"))
    
    return bin
    
@persistent
def save_scene_to_file(scene, file_path):
    return_frame = scene.frame_current
    
    bin = get_gpla_file_allframes(scene)
    
    if file_path is not None:
        with open(file_path, "wb") as f:
            f.write(bytes(bin))
    else:
        return 1
    
    scene.frame_set(return_frame)
    return 0

def get_frame_info_binary():
    frame_info = bytearray() 
    frame_info.extend(("FRAME   ").encode("utf8"))
    
    frame_info.extend(("focalLen").encode("utf8"))
    frame_info.extend(struct.pack("d", -0.05 * bpy.data.cameras[0].lens))
    
    frame_info.extend(("OBJECTS ").encode("utf8"))
    
    if (bpy.app.version[0] > 4) or (bpy.app.version[0] == 4 and bpy.app.version[1] >= 3):
        for object in bpy.data.objects:
            if object.visible_get() and object.type == 'GREASEPENCIL':
                dg =  bpy.context.evaluated_depsgraph_get()
                obj = object.evaluated_get(dg)
                frame_info.extend(("OBJECT  ").encode("utf8"))
                
                # matrix
                frame_info.extend(("MATRIX  ").encode("utf8"))
                camera_space = bpy.context.scene.camera.matrix_world.inverted() @ obj.matrix_world
                for i in range(4):
                    for j in range(4):
                        frame_info.extend(struct.pack("d", camera_space[i][j]))
                frame_info.extend(("DONE    ").encode("utf8"))
                
                # strokes
                frame_info.extend(("STROKES ").encode("utf8"))
                layers = obj.data.layers
                for layer in layers:
                    strokes = layer.frames.data.current_frame().drawing.strokes
                    for stroke in strokes:
                        frame_info.extend(("STROKE  ").encode("utf8"))
                    
                        frame_info.extend(("vertexCt").encode("utf8"))
                        frame_info.extend(len(stroke.points).to_bytes(8, "little"))
                    
                        frame_info.extend(("VERTICES").encode("utf8"))
                        for vert in stroke.points:
                            frame_info.extend(struct.pack("d", vert.position.x))
                            frame_info.extend(struct.pack("d", vert.position.y))
                            frame_info.extend(struct.pack("d", vert.position.z))
                        # VERTICES
                        frame_info.extend(("DONE    ").encode("utf8"))
                
                        # STROKE
                        frame_info.extend(("DONE    ").encode("utf8"))
                
                # STROKES
                frame_info.extend(("DONE    ").encode("utf8"))
                
                # OBJECT
                frame_info.extend(("DONE    ").encode("utf8"))
    else:
        for object in bpy.data.objects: 
            if object.visible_get() and obj.type == 'GPENCIL':
                dg =  bpy.context.evaluated_depsgraph_get()
                obj = object.evaluated_get(dg)
                frame_info.extend(("OBJECT  ").encode("utf8"))
                
                # matrix
                frame_info.extend(("MATRIX  ").encode("utf8"))
                camera_space = bpy.context.scene.camera.matrix_world.inverted() @ obj.matrix_world
                for i in range(4):
                    for j in range(4):
                        frame_info.extend(camera_space[i][j].to_bytes(8, "little"))
                # MATRIX
                frame_info.extend(("DONE    ").encode("utf8"))
                
                # strokes
                frame_info.extend(("STROKES ").encode("utf8"))
                layers = obj.data.layers
                for layer in layers:
                    strokes = layer.frames.data.active_frame.strokes
                    for stroke in strokes:
                        frame_info.extend(("STROKE  ").encode("utf8"))
                        
                        frame_info.extend(("vertexCt").encode("utf8"))
                        frame_info.extend(len(stroke.points).to_bytes(8, "little"))
                        
                        frame_info.extend(("VERTICES").encode("utf8"))
                        for vert in stroke.points:
                            frame_info.extend(struct.pack("d", vert.co[0]))
                            frame_info.extend(struct.pack("d", vert.co[1]))
                            frame_info.extend(struct.pack("d", vert.co[2]))
                        # VERTICES
                        frame_info.extend(("DONE    ").encode("utf8"))
                    
                        # STROKE
                        frame_info.extend(("DONE    ").encode("utf8"))
                
                # STROKES
                frame_info.extend(("DONE    ").encode("utf8"))
                
                # OBJECT
                frame_info.extend(("DONE    ").encode("utf8"))
    
    # OBJECTS
    frame_info.extend(("DONE    ").encode("utf8"))
    
    # FRAME
    frame_info.extend(("DONE    ").encode("utf8"))
    
    return frame_info
    

@persistent
def send_scene_to_osci_render(scene):
    global sock

    if sock is not None:
        bin = get_gpla_file(scene)
        try:
            sock.sendall(base64.b64encode(bytes(bin)) + "\n".encode("utf8"))
        except socket.error as exp:
            sock = None


operations = [OBJECT_PT_osci_render_settings, osci_render_connect, osci_render_close, osci_render_save]


def register():
    bpy.types.Scene.oscirenderPort = bpy.props.IntProperty(name="osci-render port",description="The port through which osci-render will connect",min=51600,max=51699,default=51677)
    bpy.app.handlers.frame_change_pre.append(send_scene_to_osci_render)
    bpy.app.handlers.depsgraph_update_post.append(send_scene_to_osci_render)
    atexit.register(close_osci_render)
    for operation in operations:
        bpy.utils.register_class(operation)


def unregister():
    del bpy.types.Object.oscirenderPort
    bpy.app.handlers.frame_change_pre.remove(send_scene_to_osci_render)
    bpy.app.handlers.depsgraph_update_post.remove(send_scene_to_osci_render)
    atexit.unregister(close_osci_render)
    for operation in reversed(operations):
        bpy.utils.unregister_class(operation)


if __name__ == "__main__":
    register()
