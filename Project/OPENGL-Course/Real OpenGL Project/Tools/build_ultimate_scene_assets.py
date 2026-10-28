"""
Builds the local (untracked) assets of Assets/Scenes/Ultimate_Scene.

    pip install numpy pillow scipy pyfqmr
    python Tools/build_ultimate_scene_assets.py          (from the project root)

1. Poly Haven terrain materials, 4K JPG   -> Assets/Textures/Ultimate_Scene/<id>/
2. Poly Haven props, 2K glTF              -> Assets/Models/Ultimate_Scene/<id>/
3. Game-ready incense cedars with LODs, built from the XfrogPlants OBJs in
   Assets/Models/PBR Vegetation/CL04_CalocedrusDecurrens_OBJ -> Assets/Models/Ultimate_Scene/Cedar_<n>/

Poly Haven assets are CC0 (https://polyhaven.com/license). Files that already exist with the
right checksum are skipped, so the script can be re-run at any time.
"""

import hashlib
import json
import os
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEX_DIR = os.path.join(ROOT, "Assets", "Textures", "Ultimate_Scene")
MODEL_DIR = os.path.join(ROOT, "Assets", "Models", "Ultimate_Scene")
CEDAR_SRC = os.path.join(ROOT, "Assets", "Models", "PBR Vegetation", "CL04_CalocedrusDecurrens_OBJ")

API = "https://api.polyhaven.com"
USER_AGENT = "RAMY-Engine-AssetBuilder/1.0"

# Terrain layers (see Ultimate_Scene.json): shore mud, forest floor, needle litter, rocky ground, cliffs
TERRAIN_TEXTURES = ["brown_mud_leaves_01", "forrest_ground_01", "forest_leaves_04", "rocky_trail", "mossy_rock"]
TERRAIN_MAPS = {"Diffuse": "diff", "nor_gl": "nor_gl", "Displacement": "disp"}
TERRAIN_RES = "4k"

PROPS = ["fern_02", "rock_moss_set_01", "rock_moss_set_02", "boulder_01", "tree_stump_01",
         "tree_stump_02", "dead_tree_trunk", "dry_branches_medium_01"]
PROP_RES = "2k"
ALPHA_PROPS = ["fern_02"]  # glTF ships a JPG diffuse; the cutout mask is a separate map


def fetch_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def md5_of(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest, md5=None):
    if os.path.exists(dest) and (md5 is None or md5_of(dest) == md5):
        return False
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + ".part"
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=300) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    if md5 and md5_of(tmp) != md5:
        os.remove(tmp)
        raise RuntimeError(f"Checksum mismatch for {url}")
    # Antivirus / thumbnail generators briefly lock freshly written files on Windows
    for attempt in range(20):
        try:
            os.replace(tmp, dest)
            return True
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.5)


def download_terrain_textures():
    for asset in TERRAIN_TEXTURES:
        files = fetch_json(f"{API}/files/{asset}")
        for key, suffix in TERRAIN_MAPS.items():
            entry = files[key][TERRAIN_RES]["jpg"]
            dest = os.path.join(TEX_DIR, asset, f"{asset}_{suffix}_{TERRAIN_RES}.jpg")
            if download(entry["url"], dest, entry.get("md5")):
                print(f"  downloaded {os.path.relpath(dest, ROOT)}")
    print("Terrain textures ready.")


def download_props():
    for asset in PROPS:
        gltf = fetch_json(f"{API}/files/{asset}")["gltf"][PROP_RES]["gltf"]
        folder = os.path.join(MODEL_DIR, asset)
        if download(gltf["url"], os.path.join(folder, f"{asset}.gltf"), gltf.get("md5")):
            print(f"  downloaded {asset}.gltf")
        for rel, inc in gltf.get("include", {}).items():
            if download(inc["url"], os.path.join(folder, rel), inc.get("md5")):
                print(f"  downloaded {asset}/{rel}")
    alpha_maps = {}
    for asset in ALPHA_PROPS:
        entry = fetch_json(f"{API}/files/{asset}")["Alpha"][PROP_RES]["jpg"]
        alpha_maps[asset] = os.path.join(MODEL_DIR, asset, "textures", f"{asset}_alpha_{PROP_RES}.jpg")
        download(entry["url"], alpha_maps[asset], entry.get("md5"))
    print("Props downloaded.")
    return alpha_maps


def main():
    steps = sys.argv[1:] or ["textures", "props", "cedars"]
    if "textures" in steps:
        download_terrain_textures()
    if "props" in steps or "cedars" in steps:
        import ultimate_scene_meshes as meshes  # needs numpy, pillow, scipy, pyfqmr
        if "props" in steps:
            meshes.build_props(MODEL_DIR, download_props())
        if "cedars" in steps:
            meshes.build_cedars(CEDAR_SRC, MODEL_DIR)


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    main()
