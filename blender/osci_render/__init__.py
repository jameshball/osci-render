import bpy
import socket
import atexit
import struct
import base64
from bpy.props import StringProperty
from bpy.app.handlers import persistent
from bpy_extras.io_utils import ExportHelper

HOST = "localhost"

sock = None


GPLA_MAJOR = 2
GPLA_MINOR = 0
GPLA_PATCH = 0


class OBJECT_PT_osci_render_settings(bpy.types.Panel):
    bl_idname = "OBJECT_PT_osci_render_settings"
    bl_label = "osci-render settings"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "render"

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
            if context.scene.camera is None:
                self.report({"WARNING"}, "Choose a scene camera before connecting")
                return {"CANCELLED"}
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                if hasattr(socket, "SO_NOSIGPIPE"):
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_NOSIGPIPE, 1)
                sock.settimeout(1)
                sock.connect((HOST, context.scene.oscirenderPort))
                send_bytes(base64.b64encode(get_gpla_file(context.scene)) + b"\n")
            except (OSError, ValueError, AttributeError) as error:
                close_osci_render()
                self.report({"WARNING"}, f"Could not connect to osci-render: {error}")
                return {"CANCELLED"}
                
        return {"FINISHED"}
        
class osci_render_save(bpy.types.Operator, ExportHelper):
    bl_label = "Save Line Art"
    bl_idname = "render.osci_render_save"
    bl_description = "Save line art to the chosen file"
    filename_ext = ".gpla"

    filter_glob: StringProperty(default="*.gpla", options={"HIDDEN"})

    def execute(self, context):
        try:
            save_scene_to_file(context.scene, bpy.path.ensure_ext(self.filepath, self.filename_ext))
        except (OSError, ValueError, RuntimeError) as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        self.report({"INFO"}, "Line art saved")
        return {"FINISHED"}


class osci_render_close(bpy.types.Operator):
    bl_label = "Close osci-render connection"
    bl_idname = "render.osci_render_close"

    def execute(self, context):
        close_osci_render()
        
        return {"FINISHED"}


def close_osci_render():
    global sock
    if sock is not None:
        try:
            send_bytes("CLOSE\n".encode('utf-8'))
        except OSError:
            pass
        finally:
            sock.close()
            sock = None

def send_bytes(data):
    view = memoryview(data)
    flags = getattr(socket, "MSG_NOSIGNAL", 0)
    while view:
        sent = sock.send(view, flags)
        if sent == 0:
            raise OSError("osci-render connection closed")
        view = view[sent:]

def gpla_header(scene, frame_count):
    return (b"GPLA    " + struct.pack("<3Q", GPLA_MAJOR, GPLA_MINOR, GPLA_PATCH)
            + b"FILE    fCount  " + struct.pack("<Q", frame_count)
            + b"fRate   " + struct.pack("<Q", scene.render.fps)
            + b"DONE    ")  # FILE


def get_gpla_file(scene):
    return gpla_header(scene, 1) + get_frame_info_binary() + b"END GPLA"


def save_scene_to_file(scene, file_path):
    if scene.camera is None:
        raise ValueError("Choose a scene camera before exporting line art")
    return_frame = scene.frame_current
    return_subframe = scene.frame_subframe
    try:
        with open(file_path, "wb") as output:
            output.write(gpla_header(scene, scene.frame_end - scene.frame_start + 1))
            for frame in range(scene.frame_start, scene.frame_end + 1):
                scene.frame_set(frame)
                output.write(get_frame_info_binary())
            output.write(b"END GPLA")
    finally:
        scene.frame_set(return_frame, subframe=return_subframe)


def get_frame_info_binary():
    # GPLA sections are nested. Each DONE closes the innermost open section.
    scene = bpy.context.scene
    if scene.camera is None:
        raise ValueError("Choose a scene camera before exporting line art")
    depsgraph = bpy.context.evaluated_depsgraph_get()
    camera = scene.camera.evaluated_get(depsgraph)
    camera_inverse = camera.matrix_world.inverted()
    frame_info = bytearray(b"FRAME   focalLen")
    frame_info.extend(struct.pack("<d", -0.05 * camera.data.lens))
    frame_info.extend(b"OBJECTS ")
    modern = bpy.app.version >= (4, 3, 0)
    object_type = 'GREASEPENCIL' if modern else 'GPENCIL'
    for original in bpy.context.view_layer.objects:
        if original.type != object_type or not original.visible_get():
            continue
        obj = original.evaluated_get(depsgraph)
        frame_info.extend(b"OBJECT  MATRIX  ")
        camera_space = camera_inverse @ obj.matrix_world
        frame_info.extend(struct.pack("<16d", *(value for row in camera_space for value in row)))
        # Close MATRIX, then open STROKES.
        frame_info.extend(b"DONE    STROKES ")
        for layer in obj.data.layers:
            if layer.hide:
                continue
            frame = layer.current_frame() if modern else layer.active_frame
            if frame is None:
                continue
            strokes = frame.drawing.strokes if modern else frame.strokes
            for stroke in strokes:
                points = [point.position if modern else point.co for point in stroke.points]
                cyclic = stroke.cyclic if modern else stroke.use_cyclic
                if cyclic and points:
                    points.append(points[0])
                frame_info.extend(b"STROKE  vertexCt")
                frame_info.extend(struct.pack("<Q", len(points)))
                frame_info.extend(b"VERTICES")
                for point in points:
                    frame_info.extend(struct.pack("<3d", *point))
                # Close VERTICES, then STROKE.
                frame_info.extend(b"DONE    DONE    ")
        # Close STROKES, then OBJECT.
        frame_info.extend(b"DONE    DONE    ")
    # Close OBJECTS, then FRAME.
    frame_info.extend(b"DONE    DONE    ")
    return frame_info


@persistent
def send_scene_to_osci_render(scene):
    global sock

    if sock is not None:
        try:
            bin = get_gpla_file(scene)
            send_bytes(base64.b64encode(bytes(bin)) + "\n".encode("utf8"))
        except (OSError, ValueError, AttributeError):
            close_osci_render()


operations = [OBJECT_PT_osci_render_settings, osci_render_connect, osci_render_close, osci_render_save]


def register():
    bpy.types.Scene.oscirenderPort = bpy.props.IntProperty(name="osci-render port",description="The port through which osci-render will connect",min=51600,max=51699,default=51677)
    for handlers in (bpy.app.handlers.frame_change_post, bpy.app.handlers.depsgraph_update_post):
        if send_scene_to_osci_render not in handlers:
            handlers.append(send_scene_to_osci_render)
    atexit.unregister(close_osci_render)
    atexit.register(close_osci_render)
    for operation in operations:
        if not operation.is_registered:
            bpy.utils.register_class(operation)


def unregister():
    close_osci_render()
    if hasattr(bpy.types.Scene, "oscirenderPort"):
        del bpy.types.Scene.oscirenderPort
    for handlers in (bpy.app.handlers.frame_change_post, bpy.app.handlers.depsgraph_update_post):
        if send_scene_to_osci_render in handlers:
            handlers.remove(send_scene_to_osci_render)
    atexit.unregister(close_osci_render)
    for operation in reversed(operations):
        if operation.is_registered:
            bpy.utils.unregister_class(operation)


if __name__ == "__main__":
    register()
