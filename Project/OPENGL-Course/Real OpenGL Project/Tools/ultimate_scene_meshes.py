"""
Mesh processing for build_ultimate_scene_assets.py.

- glTF 2.0 / OBJ reading and glTF writing (one node per mesh, transforms baked: the engine's
  Model loader ignores node transforms, and the Scatter node picks LODs by mesh name:
  "<Part>", "<Part>_LOD1", "<Part>_LOD2").
- Decimation that keeps the texture mapping: pyfqmr (quadric error metrics) simplifies the
  position-welded mesh, then every new triangle takes its UVs from the closest original triangle
  that faces the same way (barycentric transfer). All three corners use the same source
  triangle, so no triangle straddles a UV seam.
- Foliage LODs by thinning alpha cards: whole cards are dropped at random and the survivors are
  scaled up around their centre so the canopy keeps its silhouette and coverage.
"""

import json
import os
import struct

import numpy as np
from PIL import Image

COMPONENT_DTYPES = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}
TYPE_SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


# ======================================================================================
# glTF reading
# ======================================================================================
def _read_accessor(gltf, buffers, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    dtype = np.dtype(COMPONENT_DTYPES[acc["componentType"]])
    comps = TYPE_SIZES[acc["type"]]
    offset = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or dtype.itemsize * comps
    arr = np.ndarray((acc["count"], comps), dtype, buffer=buffers[view["buffer"]], offset=offset,
                     strides=(stride, dtype.itemsize))
    return np.array(arr)


def _node_matrix(node):
    if "matrix" in node:
        return np.array(node["matrix"], np.float64).reshape(4, 4).T
    t = np.array(node.get("translation", [0, 0, 0]), np.float64)
    x, y, z, w = node.get("rotation", [0, 0, 0, 1])
    s = np.array(node.get("scale", [1, 1, 1]), np.float64)
    r = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    m = np.eye(4)
    m[:3, :3] = r * s
    m[:3, 3] = t
    return m


def read_gltf(path):
    """Returns (gltf json, [mesh parts]) with node transforms baked into the vertices.
    Each part: dict(name, positions, normals, uvs, indices, material)."""
    with open(path, encoding="utf-8") as f:
        gltf = json.load(f)
    folder = os.path.dirname(path)
    buffers = []
    for b in gltf["buffers"]:
        with open(os.path.join(folder, b["uri"]), "rb") as f:
            buffers.append(f.read())

    parts = []

    def walk(node_index, parent):
        node = gltf["nodes"][node_index]
        world = parent @ _node_matrix(node)
        if "mesh" in node:
            normal_matrix = np.linalg.inv(world[:3, :3]).T
            for prim in gltf["meshes"][node["mesh"]]["primitives"]:
                attr = prim["attributes"]
                pos = _read_accessor(gltf, buffers, attr["POSITION"]).astype(np.float64)
                nrm = _read_accessor(gltf, buffers, attr["NORMAL"]).astype(np.float64)
                uv = _read_accessor(gltf, buffers, attr["TEXCOORD_0"]).astype(np.float64)
                idx = _read_accessor(gltf, buffers, prim["indices"]).reshape(-1).astype(np.int64)
                pos = pos @ world[:3, :3].T + world[:3, 3]
                nrm = nrm @ normal_matrix.T
                nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
                parts.append(dict(name=node.get("name", f"node{node_index}"), positions=pos, normals=nrm,
                                  uvs=uv, indices=idx, material=prim.get("material", 0)))
        for child in node.get("children", []):
            walk(child, world)

    for root in gltf["scenes"][gltf.get("scene", 0)]["nodes"]:
        walk(root, np.eye(4))
    return gltf, parts


def merge_parts(parts):
    """Concatenates several mesh parts into one (same material assumed)."""
    out = dict(name=parts[0]["name"], positions=[], normals=[], uvs=[], indices=[], material=parts[0]["material"])
    base = 0
    for p in parts:
        out["positions"].append(p["positions"])
        out["normals"].append(p["normals"])
        out["uvs"].append(p["uvs"])
        out["indices"].append(p["indices"] + base)
        base += len(p["positions"])
    for k in ("positions", "normals", "uvs", "indices"):
        out[k] = np.concatenate(out[k])
    return out


# ======================================================================================
# OBJ reading (triangulated, per-material corner indices)
# ======================================================================================
def read_obj(path):
    """Returns (positions, uvs, normals, {material: (F,3,3) int array of v/vt/vn indices})."""
    v, vt, vn = [], [], []
    faces = {}
    current = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("v "):
                v.append(line.split()[1:4])
            elif line.startswith("vt "):
                vt.append(line.split()[1:3])
            elif line.startswith("vn "):
                vn.append(line.split()[1:4])
            elif line.startswith("usemtl"):
                current = line.split(maxsplit=1)[1].strip()
                faces.setdefault(current, [])
            elif line.startswith("f "):
                corners = [c.split("/") for c in line.split()[1:]]
                tri = [[int(c[0]), int(c[1]) if len(c) > 1 and c[1] else 0, int(c[2]) if len(c) > 2 and c[2] else 0]
                       for c in corners]
                for k in range(1, len(tri) - 1):  # fan-triangulate polygons
                    faces[current].append([tri[0], tri[k], tri[k + 1]])
    positions = np.array(v, np.float64)
    uvs = np.array(vt, np.float64)
    normals = np.array(vn, np.float64)
    out = {}
    for mat, fl in faces.items():
        arr = np.array(fl, np.int64) - 1  # OBJ is 1-based
        out[mat] = arr
    return positions, uvs, normals, out


# ======================================================================================
# glTF writing
# ======================================================================================
def write_gltf(path, meshes, materials):
    """meshes: list of dict(name, positions, normals, uvs, indices, material) -> one node per mesh.
    materials: list of dict(name, base_color (uri), normal (uri or None), alpha_mask (bool))."""
    blob = bytearray()
    accessors, views = [], []

    def add(data, target, acc_type, comp_type, with_bounds=False):
        while len(blob) % 4:
            blob.append(0)
        raw = data.tobytes()
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(raw), "target": target})
        blob.extend(raw)
        acc = {"bufferView": len(views) - 1, "componentType": comp_type, "count": int(len(data)), "type": acc_type}
        if with_bounds:
            acc["min"] = data.min(axis=0).tolist()
            acc["max"] = data.max(axis=0).tolist()
        accessors.append(acc)
        return len(accessors) - 1

    gl_meshes, nodes = [], []
    for m in meshes:
        pos = np.ascontiguousarray(m["positions"], np.float32)
        nrm = np.ascontiguousarray(m["normals"], np.float32)
        uv = np.ascontiguousarray(m["uvs"], np.float32)
        idx = np.ascontiguousarray(m["indices"], np.uint32)
        prim = {
            "attributes": {
                "POSITION": add(pos, 34962, "VEC3", 5126, True),
                "NORMAL": add(nrm, 34962, "VEC3", 5126),
                "TEXCOORD_0": add(uv, 34962, "VEC2", 5126),
            },
            "indices": add(idx, 34963, "SCALAR", 5125),
            "material": m["material"],
        }
        gl_meshes.append({"name": m["name"], "primitives": [prim]})
        nodes.append({"name": m["name"], "mesh": len(gl_meshes) - 1})

    images, textures, gl_materials = [], [], []

    def texture_index(uri):
        for i, img in enumerate(images):
            if img["uri"] == uri:
                return i
        images.append({"uri": uri})
        textures.append({"source": len(images) - 1})
        return len(images) - 1

    for mat in materials:
        g = {"name": mat["name"], "doubleSided": True,
             "pbrMetallicRoughness": {"baseColorTexture": {"index": texture_index(mat["base_color"])},
                                      "metallicFactor": 0.0, "roughnessFactor": 0.9}}
        if mat.get("normal"):
            g["normalTexture"] = {"index": texture_index(mat["normal"])}
        if mat.get("alpha_mask"):
            g["alphaMode"] = "MASK"
            g["alphaCutoff"] = 0.5
        gl_materials.append(g)

    bin_name = os.path.splitext(os.path.basename(path))[0] + ".bin"
    gltf = {
        "asset": {"version": "2.0", "generator": "RAMY build_ultimate_scene_assets.py"},
        "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes, "meshes": gl_meshes, "materials": gl_materials,
        "textures": textures, "images": images,
        "accessors": accessors, "bufferViews": views,
        "buffers": [{"uri": bin_name, "byteLength": len(blob)}],
    }
    with open(os.path.join(os.path.dirname(path), bin_name), "wb") as f:
        f.write(blob)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(gltf, f, indent=1)


# ======================================================================================
# Geometry helpers
# ======================================================================================
def weld_corners(corner_pos, corner_nrm, corner_uv):
    """(F*3, ...) corner attributes -> indexed mesh, merging identical corners."""
    key = np.concatenate([np.round(corner_pos, 5), np.round(corner_nrm, 3), np.round(corner_uv, 5)], axis=1)
    _, first, inverse = np.unique(key, axis=0, return_index=True, return_inverse=True)
    return corner_pos[first], corner_nrm[first], corner_uv[first], inverse.reshape(-1)


def smooth_normals(positions, tris):
    face_n = np.cross(positions[tris[:, 1]] - positions[tris[:, 0]], positions[tris[:, 2]] - positions[tris[:, 0]])
    vn = np.zeros_like(positions)
    for k in range(3):
        np.add.at(vn, tris[:, k], face_n)
    return vn / np.maximum(np.linalg.norm(vn, axis=1, keepdims=True), 1e-12)


def weld_positions(positions, tris, decimals=5):
    """Merges coincident vertices (UV seams) so the simplifier sees one closed surface."""
    _, first, inverse = np.unique(np.round(positions, decimals), axis=0, return_index=True, return_inverse=True)
    return positions[first], inverse.reshape(-1)[tris]


def decimate(corner_pos, corner_uv, corner_nrm, target_tris, aggressiveness=7):
    """Simplifies a triangle soup given as (F,3,·) corner arrays to ~target_tris triangles,
    transferring UVs from the original surface. Returns an indexed mesh part (no name/material)."""
    import pyfqmr
    from scipy.spatial import cKDTree

    n_faces = corner_pos.shape[0]
    flat_pos = corner_pos.reshape(-1, 3)
    if target_tris >= n_faces:
        p, n, uv, idx = weld_corners(flat_pos, corner_nrm.reshape(-1, 3), corner_uv.reshape(-1, 2))
        return dict(positions=p, normals=n, uvs=uv, indices=idx)

    weld_p, weld_t = weld_positions(flat_pos, np.arange(n_faces * 3).reshape(-1, 3))
    simp = pyfqmr.Simplify()
    simp.setMesh(weld_p.astype(np.float64), weld_t.astype(np.int32))
    simp.simplify_mesh(target_count=int(target_tris), aggressiveness=aggressiveness, preserve_border=False, verbose=False)
    new_p, new_t, _ = simp.getMesh()
    new_p = np.asarray(new_p, np.float64)
    new_t = np.asarray(new_t, np.int64)
    new_t = new_t[(new_t[:, 0] != new_t[:, 1]) & (new_t[:, 1] != new_t[:, 2]) & (new_t[:, 0] != new_t[:, 2])]

    # Source triangle per new triangle: among the nearest originals, the best-aligned one
    src_a, src_b, src_c = corner_pos[:, 0], corner_pos[:, 1], corner_pos[:, 2]
    src_n = np.cross(src_b - src_a, src_c - src_a)
    src_n /= np.maximum(np.linalg.norm(src_n, axis=1, keepdims=True), 1e-12)
    tree = cKDTree((src_a + src_b + src_c) / 3.0)

    tri_p = new_p[new_t]  # (T,3,3)
    tri_n = np.cross(tri_p[:, 1] - tri_p[:, 0], tri_p[:, 2] - tri_p[:, 0])
    tri_n /= np.maximum(np.linalg.norm(tri_n, axis=1, keepdims=True), 1e-12)
    k = min(12, n_faces)
    dist, cand = tree.query(tri_p.mean(axis=1), k=k)
    if k == 1:
        dist, cand = dist[:, None], cand[:, None]
    align = np.einsum("tj,tkj->tk", tri_n, src_n[cand])
    score = align - dist / np.maximum(dist[:, -1:], 1e-9) * 0.5
    src = cand[np.arange(len(cand)), np.argmax(score, axis=1)]

    # Fallback UVs: every corner extrapolated from the triangle's single source triangle
    out_uv = np.stack([_barycentric(tri_p[:, k], src_a[src], src_b[src], src_c[src], -1.5, 2.5) @ np.eye(3)
                       for k in range(3)], axis=1)  # (T,3,3) barycentrics
    out_uv = np.einsum("tck,tkj->tcj", out_uv, corner_uv[src])

    # Preferred UVs: each corner takes the UV of its closest point on the original surface.
    # Photogrammetry atlases are split into many small islands; where all three corners land
    # on the same island this is exact, so only island-straddling triangles use the fallback.
    charts = _uv_islands(corner_pos, corner_uv)
    q = tri_p.reshape(-1, 3)
    _, cand = tree.query(q, k=min(8, n_faces))
    if cand.ndim == 1:
        cand = cand[:, None]
    best_d = np.full(len(q), np.inf)
    best_src = np.zeros(len(q), np.int64)
    best_bary = np.zeros((len(q), 3))
    for j in range(cand.shape[1]):
        cj = cand[:, j]
        bary = _barycentric(q, src_a[cj], src_b[cj], src_c[cj], 0.0, 1.0)
        bary /= np.maximum(bary.sum(axis=1, keepdims=True), 1e-12)
        closest = bary[:, :1] * src_a[cj] + bary[:, 1:2] * src_b[cj] + bary[:, 2:] * src_c[cj]
        d = np.linalg.norm(q - closest, axis=1)
        better = d < best_d
        best_d[better], best_src[better], best_bary[better] = d[better], cj[better], bary[better]
    corner_island = charts[best_src].reshape(-1, 3)
    same_island = (corner_island[:, 0] == corner_island[:, 1]) & (corner_island[:, 1] == corner_island[:, 2])
    exact_uv = np.einsum("nk,nkj->nj", best_bary, corner_uv[best_src]).reshape(-1, 3, 2)
    out_uv[same_island] = exact_uv[same_island]

    vn = smooth_normals(new_p, new_t)
    p, n, uv, idx = weld_corners(tri_p.reshape(-1, 3), vn[new_t].reshape(-1, 3), out_uv.reshape(-1, 2))
    return dict(positions=p, normals=n, uvs=uv, indices=idx)


def _barycentric(points, a, b, c, lo, hi):
    """Barycentric coordinates of points projected into the planes of triangles abc, clipped to [lo, hi]."""
    e0, e1, vp = b - a, c - a, points - a
    d00 = np.einsum("ij,ij->i", e0, e0)
    d01 = np.einsum("ij,ij->i", e0, e1)
    d11 = np.einsum("ij,ij->i", e1, e1)
    d20 = np.einsum("ij,ij->i", vp, e0)
    d21 = np.einsum("ij,ij->i", vp, e1)
    denom = np.maximum(d00 * d11 - d01 * d01, 1e-18)
    bv = (d11 * d20 - d01 * d21) / denom
    bw = (d00 * d21 - d01 * d20) / denom
    return np.clip(np.stack([1.0 - bv - bw, bv, bw], axis=1), lo, hi)


def _uv_islands(corner_pos, corner_uv):
    """Island id per triangle: triangles sharing a corner with the same position AND uv."""
    key = np.concatenate([np.round(corner_pos.reshape(-1, 3), 5), np.round(corner_uv.reshape(-1, 2), 5)], axis=1)
    _, inverse = np.unique(key, axis=0, return_inverse=True)
    return card_components(inverse.reshape(-1, 3))


def card_components(position_tris):
    """Connected components (cards) of a triangle list given by shared position indices."""
    n = int(position_tris.max()) + 1
    parent = np.arange(n)

    def find(x):
        root = x
        while parent[root] != root:
            root = parent[root]
        while parent[x] != root:
            parent[x], x = root, parent[x]
        return root

    for t in position_tris:
        r0, r1, r2 = find(t[0]), find(t[1]), find(t[2])
        parent[r1] = r0
        parent[find(r2)] = r0
    roots = np.array([find(t[0]) for t in position_tris])
    _, comp = np.unique(roots, return_inverse=True)
    return comp


def thin_cards(corner_pos, corner_uv, corner_nrm, components, keep, seed=1):
    """Keeps a random `keep` fraction of the cards, scaling survivors to preserve coverage."""
    rng = np.random.default_rng(seed)
    n_comp = components.max() + 1
    kept = rng.random(n_comp) < keep
    face_mask = kept[components]
    pos = corner_pos[face_mask].copy()
    comp = components[face_mask]
    centers = np.zeros((n_comp, 3))
    counts = np.zeros(n_comp)
    np.add.at(centers, comp, pos.mean(axis=1))
    np.add.at(counts, comp, 1)
    centers /= np.maximum(counts, 1)[:, None]
    scale = min(1.0 / np.sqrt(keep), 1.8)
    pos = centers[comp][:, None, :] + (pos - centers[comp][:, None, :]) * scale
    p, n, uv, idx = weld_corners(pos.reshape(-1, 3), corner_nrm[face_mask].reshape(-1, 3),
                                 corner_uv[face_mask].reshape(-1, 2))
    return dict(positions=p, normals=n, uvs=uv, indices=idx)


def drop_small_components(corner_pos, corner_uv, corner_nrm, min_size):
    """Removes disconnected pieces (twigs) whose bounding-box diagonal is below min_size."""
    if min_size <= 0:
        return corner_pos, corner_uv, corner_nrm
    _, welded = weld_positions(corner_pos.reshape(-1, 3), np.arange(len(corner_pos) * 3).reshape(-1, 3))
    comp = card_components(welded)
    n = comp.max() + 1
    lo = np.full((n, 3), np.inf)
    hi = np.full((n, 3), -np.inf)
    np.minimum.at(lo, comp, corner_pos.min(axis=1))
    np.maximum.at(hi, comp, corner_pos.max(axis=1))
    keep = (np.linalg.norm(hi - lo, axis=1) >= min_size)[comp]
    return corner_pos[keep], corner_uv[keep], corner_nrm[keep]


def part_to_corners(part):
    t = part["indices"].reshape(-1, 3)
    return part["positions"][t], part["uvs"][t], part["normals"][t]


def ground_and_center(parts):
    """Moves a model so its footprint is centred on the origin and its base sits at y = 0."""
    allp = np.concatenate([p["positions"] for p in parts])
    lo, hi = allp.min(axis=0), allp.max(axis=0)
    offset = np.array([(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2])
    for p in parts:
        p["positions"] = p["positions"] - offset
    return parts


def make_rgba(color_path, alpha_path, out_path):
    color = Image.open(color_path).convert("RGB")
    alpha = Image.open(alpha_path).convert("L").resize(color.size, Image.BILINEAR)
    rgba = color.copy()
    rgba.putalpha(alpha)
    rgba.save(out_path, optimize=True)


# ======================================================================================
# Cedars (XfrogPlants incense cedar OBJs -> glTF with bark/leaf LODs)
# ======================================================================================
CEDAR_VARIANTS = {"a": 2, "b": 4, "c": 6}        # variant -> OBJ number (18 m, 29 m, 40 m tall)
# Bark per LOD: (drop twigs smaller than this many metres, triangle budget). Far away the twigs
# are hidden by the foliage anyway; LOD2 keeps only the trunk and the main limbs.
CEDAR_BARK_LODS = ((0.0, 20000), (1.0, 5000), (2.0, 1200))
CEDAR_LEAF_KEEP = (1.0, 0.45, 0.15)              # fraction of leaf cards kept per LOD
CM_TO_M = 0.01


def build_cedars(src_dir, model_dir):
    out_dir = os.path.join(model_dir, "Cedars")
    os.makedirs(out_dir, exist_ok=True)
    tex = {
        "bark": "us_cedar_bark_diff.png", "bark_n": "us_cedar_bark_nor.png",
        "leaf": "us_cedar_leaf_diff_alpha.png", "leaf_n": "us_cedar_leaf_nor.png",
    }
    pairs = [("CL04_Bark01_dif_su.png", tex["bark"]), ("CL04_Bark01_nml_su.png", tex["bark_n"]),
             ("CL04_Leaf01_nml_su.png", tex["leaf_n"])]
    for src, dst in pairs:
        dst_path = os.path.join(out_dir, dst)
        if not os.path.exists(dst_path):
            Image.open(os.path.join(src_dir, src)).convert("RGB").save(dst_path, optimize=True)
    leaf_path = os.path.join(out_dir, tex["leaf"])
    if not os.path.exists(leaf_path):
        make_rgba(os.path.join(src_dir, "CL04_Leaf01Front_dif_su.png"), os.path.join(src_dir, "CL04_Leaf01_alp_su.png"), leaf_path)

    materials = [dict(name="CedarBark", base_color=tex["bark"], normal=tex["bark_n"]),
                 dict(name="CedarLeaves", base_color=tex["leaf"], normal=tex["leaf_n"], alpha_mask=True)]

    for variant, number in CEDAR_VARIANTS.items():
        out_path = os.path.join(out_dir, f"us_cedar_{variant}.gltf")
        if os.path.exists(out_path):
            continue
        obj = os.path.join(src_dir, f"OBJ_CL04_CalocedrusDecurrens_{number}.obj")
        print(f"  cedar {variant}: reading {os.path.basename(obj)}")
        positions, uvs, normals, faces = read_obj(obj)
        positions = positions * CM_TO_M
        uvs = np.column_stack([uvs[:, 0], 1.0 - uvs[:, 1]])  # OBJ (bottom-left) -> glTF (top-left)

        meshes = []
        for mat_name, label, mat_index in (("Bark01_SHD", "Bark", 0), ("Leaf01_su_SHD", "Leaves", 1)):
            f = faces[mat_name]
            cp, cuv, cn = positions[f[:, :, 0]], uvs[f[:, :, 1]], normals[f[:, :, 2]]
            if label == "Bark":
                for lod, (min_size, budget) in enumerate(CEDAR_BARK_LODS):
                    part = decimate(*drop_small_components(cp, cuv, cn, min_size), budget)
                    meshes.append(dict(part, name=label if lod == 0 else f"{label}_LOD{lod}", material=mat_index))
            else:
                comps = card_components(f[:, :, 0])
                for lod, keep in enumerate(CEDAR_LEAF_KEEP):
                    part = thin_cards(cp, cuv, cn, comps, keep, seed=7 + lod)
                    meshes.append(dict(part, name=label if lod == 0 else f"{label}_LOD{lod}", material=mat_index))
        ground_and_center_xz(meshes)
        write_gltf(out_path, meshes, materials)
        print("    " + ", ".join(f"{m['name']}={len(m['indices']) // 3}" for m in meshes))
    print("Cedars ready.")


def ground_and_center_xz(meshes):
    """Centres the trunk on the origin (x/z of the LOD0 bark base); keeps y (roots go below 0)."""
    bark = meshes[0]["positions"]
    base = bark[bark[:, 1] < bark[:, 1].min() + 1.0]
    offset = np.array([base[:, 0].mean(), 0.0, base[:, 2].mean()])
    for m in meshes:
        m["positions"] = m["positions"] - offset


# ======================================================================================
# Poly Haven props -> single-model glTFs with LODs
# ======================================================================================
# output name -> (source asset, node names to merge (None = all), LOD triangle budgets)
PROP_EXPORTS = {
    "us_fern_a": ("fern_02", ["fern_02_b"], (None, 0.5, 0.2)),
    "us_fern_b": ("fern_02", ["fern_02_c"], (None, 0.5, 0.2)),
    "us_boulder": ("boulder_01", None, (None, 12000, 2500)),
    "us_rock_a": ("rock_moss_set_01", ["rock_moss_set_01_rock01"], (None, 3000, 800)),
    "us_rock_b": ("rock_moss_set_01", ["rock_moss_set_01_rock03"], (None, 3000, 800)),
    "us_rock_c": ("rock_moss_set_02", ["rock_moss_set_02_rock12"], (None, 2500, 700)),
    "us_stump_a": ("tree_stump_01", None, (15000, 4000, 1000)),
    "us_stump_b": ("tree_stump_02", None, (15000, 4000, 1000)),
    "us_log": ("dead_tree_trunk", None, (20000, 5000, 1200)),
    "us_branches": ("dry_branches_medium_01", None, (None, 0.4, 0.15)),
}
ALPHA_PROPS = {"fern_02"}


def build_props(model_dir, alpha_maps):
    """alpha_maps: {asset: path of the downloaded alpha map} for cutout assets."""
    for out_name, (asset, node_names, budgets) in PROP_EXPORTS.items():
        folder = os.path.join(model_dir, asset)
        out_path = os.path.join(folder, out_name + ".gltf")
        if os.path.exists(out_path):
            continue
        gltf, parts = read_gltf(os.path.join(folder, asset + ".gltf"))
        if node_names:
            parts = [p for p in parts if p["name"] in node_names]
        if not parts:
            raise RuntimeError(f"{out_name}: no matching nodes in {asset}")
        part = merge_parts(ground_and_center([dict(p) for p in parts]))

        # Material: the asset's own diffuse/normal (cutout assets get an RGBA diffuse)
        img = {i: im["uri"] for i, im in enumerate(gltf["images"])}
        src_mat = gltf["materials"][part["material"]]
        diffuse = img[gltf["textures"][src_mat["pbrMetallicRoughness"]["baseColorTexture"]["index"]]["source"]]
        normal = img[gltf["textures"][src_mat["normalTexture"]["index"]]["source"]] if "normalTexture" in src_mat else None
        alpha = asset in ALPHA_PROPS
        if alpha:
            rgba_name = f"textures/us_{asset}_diff_alpha.png"
            rgba_path = os.path.join(folder, rgba_name)
            if not os.path.exists(rgba_path):
                make_rgba(os.path.join(folder, diffuse), alpha_maps[asset], rgba_path)
            diffuse = rgba_name
        materials = [dict(name=out_name, base_color=diffuse, normal=normal, alpha_mask=alpha)]

        cp, cuv, cn = part_to_corners(part)
        n_tris = len(cp)
        meshes = []
        for lod, budget in enumerate(budgets):
            if budget is None:
                lod_part = dict(positions=part["positions"], normals=part["normals"], uvs=part["uvs"], indices=part["indices"])
            else:
                target = int(budget * n_tris) if budget < 1.0 else int(budget)
                lod_part = decimate(cp, cuv, cn, target)
            meshes.append(dict(lod_part, name="Mesh" if lod == 0 else f"Mesh_LOD{lod}", material=0))
        write_gltf(out_path, meshes, materials)
        print(f"  {out_name}: " + ", ".join(f"{m['name']}={len(m['indices']) // 3}" for m in meshes))
    print("Prop models ready.")
