"""Runs inside Blender: dumps triangles of a model file as JSON for tools/import_models.py.

blender -b --factory-startup --disable-autoexec --python tools/blender_dump.py -- <job.json>
job: {"file", "out", "keep": [substr], "drop": [substr], "decimate": {substr: ratio}, "budget": max triangles}
Output: {"objects": [{"name", "tris": [[x, y, z, nx, ny, nz, u, v] * 3], "mats": [index per tri]}], "materials": [{"name", "color", "image"}]}
Coordinates are Blender world space (z up).
"""
import json
import os
import sys

import bpy

job = json.load(open(sys.argv[sys.argv.index("--") + 1], encoding="utf-8"))
path = job["file"]
bpy.ops.wm.read_factory_settings(use_empty=True)
low = path.lower()
if low.endswith(".obj"):
    bpy.ops.wm.obj_import(filepath=path)
elif low.endswith(".fbx"):
    bpy.ops.import_scene.fbx(filepath=path)
elif low.endswith(".gltf") or low.endswith(".glb"):
    bpy.ops.import_scene.gltf(filepath=path)
elif low.endswith(".blend"):
    with bpy.data.libraries.load(path) as (src, dst):
        dst.objects = src.objects
    for o in dst.objects:
        if o is not None:
            bpy.context.scene.collection.objects.link(o)


def wanted(name):
    if job.get("keep") and not any(k in name for k in job["keep"]):
        return False
    return not any(k in name for k in job.get("drop", []))


materials, mat_index = [], {}


def material(m):
    key = m.name if m else ""
    if key in mat_index:
        return mat_index[key]
    color, image = [0.8, 0.8, 0.8], ""
    if m:
        color = list(m.diffuse_color[:3])
        if m.node_tree:
            images = []
            for n in m.node_tree.nodes:
                if n.type == "BSDF_PRINCIPLED":
                    color = list(n.inputs["Base Color"].default_value[:3])
                    for link in n.inputs["Base Color"].links:
                        if link.from_node.type == "TEX_IMAGE" and link.from_node.image:
                            images.insert(0, link.from_node.image)
                if n.type == "TEX_IMAGE" and n.image:
                    images.append(n.image)
            if images:
                img = images[0]
                image = bpy.path.abspath(img.filepath) if img.filepath else img.name
    mat_index[key] = len(materials)
    materials.append({"name": key, "color": color, "image": os.path.basename(image)})
    return mat_index[key]


objects = []
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and wanted(o.name)]
if job.get("budget"):
    total = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes)
    if total > job["budget"]:
        job.setdefault("decimate", {})["*"] = job["budget"] / total
for o in meshes:
    for k, ratio in job.get("decimate", {}).items():
        if k in o.name or k == "*":
            d = o.modifiers.new("dec", "DECIMATE")
            d.ratio = ratio
            break
    o.modifiers.new("tri", "TRIANGULATE")
deps = bpy.context.evaluated_depsgraph_get()
for o in meshes:
    eo = o.evaluated_get(deps)
    me = eo.to_mesh()
    me.transform(o.matrix_world)
    if o.matrix_world.determinant() < 0:
        me.flip_normals()
    me.calc_loop_triangles()
    uv = me.uv_layers.active
    normals = me.corner_normals
    tris, mats = [], []
    for t in me.loop_triangles:
        tri = []
        for li in t.loops:
            p = me.vertices[me.loops[li].vertex_index].co
            n = normals[li].vector
            u = uv.data[li].uv if uv else (0.0, 0.0)
            tri.append([p.x, p.y, p.z, n.x, n.y, n.z, u[0], u[1]])
        tris.append(tri)
        slot = o.material_slots[t.material_index].material if t.material_index < len(o.material_slots) else None
        mats.append(material(slot))
    objects.append({"name": o.name, "tris": tris, "mats": mats})
    eo.to_mesh_clear()

with open(job["out"], "w", encoding="utf-8") as f:
    json.dump({"objects": objects, "materials": materials}, f)
print("dumped", len(objects), "objects", sum(len(o["tris"]) for o in objects), "tris")
