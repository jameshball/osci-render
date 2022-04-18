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


def my_osci_render_func(scene):
    engine_info = {"objects": []}

    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            object_info = {"name": obj.name}
            if obj.name not in col["seen_objs"]:
                col["seen_objs"][obj.name] = 1
                print("new object! ", obj.name)

                mesh = bmesh.new()
                mesh.from_mesh(obj.data)

                object_info["vertices"] = []
                # If there are bugs, the vertices here might not match up with the vert.index in edges
                for vert in mesh.verts:
                    object_info["vertices"].append({
                        "x": vert.co[0],
                        "y": vert.co[1],
                        "z": vert.co[2],
                    })

                object_info["edges"] = []
                for edge in mesh.edges:
                    for vert in edge.verts:
                        object_info["edges"].append(vert.index)

            camera_space = camera.matrix_world.inverted() @ obj.matrix_world
            object_info["matrix"] = []
            for i in range(4):
                for j in range(4):
                    object_info["matrix"].append(camera_space[i][j])

            engine_info["objects"].append(object_info)


    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))

    sock.sendall(json.dumps(engine_info, separators=(',', ':')).encode('utf-8'))
    sock.close()


bpy.app.handlers.frame_change_pre.clear()
bpy.app.handlers.frame_change_pre.append(my_osci_render_func)