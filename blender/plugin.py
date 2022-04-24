import bpy
import bmesh
import socket
import json

HOST = "localhost"
PORT = 51677

# Persistent global storage for knowing which meshes
# have already been sent to osci-render
bpy.context.scene.collection["osci_render"] = {}
col = bpy.context.scene.collection["osci_render"]
col["seen_objs"] = {}

camera = bpy.context.scene.camera

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
            self.layout.operator("render.osci_render_connect", text="Connect to osci-render")
        else:
            self.layout.operator("render.osci_render_close", text="Close osci-render connection")


class osci_render_connect(bpy.types.Operator):
    bl_label = "Connect to osci-render"
    bl_idname = "render.osci_render_connect"
    bl_description = "Connect to osci-render"

    def execute(self, context):
        global sock
        if sock is None:
            try:
                col["seen_objs"] = {}
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect((HOST, PORT))
                send_scene_to_osci_render(bpy.context.scene)
            except OSError as exp:
                sock.close()
                sock = None

        return {"FINISHED"}


class osci_render_close(bpy.types.Operator):
    bl_label = "Close osci-render connection"
    bl_idname="render.osci_render_close"

    def execute(self, context):
        global sock
        if sock is not None:
            sock.send("CLOSE\n".encode('utf-8'))
            sock.close()
            sock = None

        return {"FINISHED"}


def send_scene_to_osci_render(scene):
    global sock
    if sock is not None:
        engine_info = {"objects": []}
        new_objs = []

        for obj in bpy.data.objects:
            if obj.type == 'MESH':
                object_info = {"name": obj.name}
                if obj.name not in col["seen_objs"]:
                    col["seen_objs"][obj.name] = 1
                    new_objs.append(obj.name)

                    mesh = bmesh.new()
                    mesh.from_mesh(obj.data)

                    object_info["vertices"] = []
                    # If there are bugs, the vertices here might not match up with the vert.index in edges/faces
                    for vert in mesh.verts:
                        object_info["vertices"].append({
                            "x": vert.co[0],
                            "y": vert.co[1],
                            "z": vert.co[2],
                        })

                    object_info["edges"] = [vert.index for edge in mesh.edges for vert in edge.verts]
                    object_info["faces"] = [[vert.index for vert in face.verts] for face in mesh.faces]

                camera_space = camera.matrix_world.inverted() @ obj.matrix_world
                object_info["matrix"] = [camera_space[i][j] for i in range(4) for j in range(4)]

                engine_info["objects"].append(object_info)


        engine_info["focalLength"] = -0.1 * bpy.data.cameras[0].lens

        try:
            json_str = json.dumps(engine_info, separators=(',', ':')) + '\n'
            sock.sendall(json_str.encode('utf-8'))
        except OSError as exc:
            # Remove all newly added objects if no connection was made
            # so that the object data will be sent on next attempt
            for obj_name in new_objs:
                col["seen_objs"].pop(obj_name)


operations = [OBJECT_PT_osci_render_settings, osci_render_connect, osci_render_close]


def register():
    bpy.app.handlers.frame_change_pre.append(send_scene_to_osci_render)
    bpy.app.handlers.depsgraph_update_post.append(send_scene_to_osci_render)
    for operation in operations:
        bpy.utils.register_class(operation)


def unregister():
    bpy.app.handlers.frame_change_pre.clear()
    bpy.app.handlers.depsgraph_update_post.clear()
    for operation in operations.reverse():
        bpy.utils.unregister_class(operation)


if __name__ == "__main__":
    register()
