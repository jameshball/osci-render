bl_info = {
    "name": "osci-render",
    "author": "James Ball", 
    "version": (1, 0, 0),
    "blender": (3, 1, 2),
    "location": "View3D",
    "description": "Addon to send frames over to osci-render",
    "warning": "Requires a camera and objects",
    "wiki_url": "https://github.com/jameshball/osci-render",
    "category": "Development",
}

import bpy
import bmesh
import socket
import json

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
                bpy.context.scene.collection["osci_render"] = {}
                bpy.context.scene.collection["osci_render"]["seen_objs"] = {}
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


def is_cyclic(spline):
    return spline.use_cyclic_u or spline.use_cyclic_v


def append_matrix(object_info, obj):
    camera_space = bpy.context.scene.camera.matrix_world.inverted() @ obj.matrix_world
    object_info["matrix"] = [camera_space[i][j] for i in range(4) for j in range(4)]
    return object_info


def send_scene_to_osci_render(scene):
    col = bpy.context.scene.collection["osci_render"]
    
    if sock is not None:
        engine_info = {"objects": []}
        new_objs = []

        for obj in bpy.data.objects:
            if obj.visible_get():
#                if obj.type == 'MESH':
#                    object_info = {"name": obj.name}
#                    if obj.name not in col["seen_objs"]:
#                        col["seen_objs"][obj.name] = 1
#                        new_objs.append(obj.name)

#                        mesh = bmesh.new()
#                        mesh.from_mesh(obj.data)

#                        object_info["vertices"] = []
#                        # If there are bugs, the vertices here might not match up with the vert.index in edges/faces
#                        for vert in mesh.verts:
#                            object_info["vertices"].append({
#                                "x": vert.co[0],
#                                "y": vert.co[1],
#                                "z": vert.co[2],
#                            })

#                        object_info["edges"] = [vert.index for edge in mesh.edges for vert in edge.verts]
#                        object_info["faces"] = [[vert.index for vert in face.verts] for face in mesh.faces]

#                    engine_info["objects"].append(append_matrix(object_info, obj))
                if obj.type == 'GPENCIL':
                    object_info = {"name": obj.name}
                    strokes = obj.data.layers.active.frames.data.active_frame.strokes
                    
                    print("found gpencil!")
                    print(strokes)
                    
                    
                    object_info["pathVertices"] = []
                    for stroke in strokes:
                        for vert in stroke.points:
                            object_info["pathVertices"].append({
                                "x": vert.co[0],
                                "y": vert.co[1],
                                "z": vert.co[2],
                            })
                        # end of path
                        object_info["pathVertices"].append({
                            "x": float("nan"),
                            "y": float("nan"),
                            "z": float("nan"),
                        })
                    
                    engine_info["objects"].append(append_matrix(object_info, obj))
    #            elif obj.type == 'CURVE':
    #                object_info = {"name": obj.name}
    #                for curve in obj.data.splines:
    #                    if curve.type == 'BEZIER':
    #                        object_info["bezierPoints"] = []
    #                        points = list(curve.bezier_points)
    #                        if is_cyclic(curve) and len(points) > 0:
    #                            points.append(points[0])
    #                        
    #                        for point in points:
    #                            for co in [point.co, point.handle_left, point.handle_right]:
    #                                object_info["bezierPoints"].append({
    #                                    "x": co[0],
    #                                    "y": co[1],
    #                                    "z": co[2],
    #                                })
    #                                
    #                        engine_info["objects"].append(append_matrix(object_info, obj))
    #                    elif curve.type == 'POLY':
    #                        object_info["polyPoints"] = []
    #                        points = list(curve.points)
    #                        if is_cyclic(curve) and len(points) > 0:
    #                            points.append(points[0])
    #                        
    #                        object_info["polyPoints"] = [{
    #                            "x": point.co[0],
    #                            "y": point.co[1],
    #                            "z": point.co[2],
    #                        } for point in points]
    #                        
    #                        engine_info["objects"].append(append_matrix(object_info, obj))
                        


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
