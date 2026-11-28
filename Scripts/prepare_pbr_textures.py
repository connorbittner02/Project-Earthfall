"""Fetch CC0 PBR texture sets from Poly Haven and pack them for build_earth_assets.py.

    py -3 Scripts/prepare_pbr_textures.py

Poly Haven publishes every asset under CC0 and serves the files without a login.
For each set this downloads the 2K albedo, DirectX normal and roughness maps into
Saved/SourceTextures/pbr/ (never committed) and writes two packed PNGs beside them:

    T_PBR_<Name>_A.png    albedo, sRGB
    T_PBR_<Name>_NR.png   R = normal X, G = normal Y, B = roughness, linear

Two textures per set instead of three keeps the terrain decal, which already reads
eight simulation maps, inside the sampler budget.
"""
import json
import os
import urllib.request

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE = os.path.join(ROOT, "Saved", "SourceTextures", "pbr")
API = "https://api.polyhaven.com/files/"
AGENT = {"User-Agent": "texture-fetch/1.0"}

# Poly Haven asset id -> name used in the project.
SETS = {
    "rubble": "Rubble",
    "concrete_wall_008": "Concrete",
    "dark_rock": "DarkRock",
    "aerial_asphalt_01": "Asphalt",
}
MAPS = (("Diffuse", "diff"), ("nor_dx", "nor_dx"), ("Rough", "rough"))


def fetch(url, path):
    if os.path.isfile(path) and os.path.getsize(path) > 0:
        return
    request = urllib.request.Request(url, headers=AGENT)
    with urllib.request.urlopen(request) as response, open(path, "wb") as handle:
        handle.write(response.read())
    print("fetched", os.path.basename(path))


def main():
    os.makedirs(STAGE, exist_ok=True)
    for asset, name in SETS.items():
        listing_path = os.path.join(STAGE, asset + ".json")
        fetch(API + asset, listing_path)
        with open(listing_path, "r", encoding="utf-8") as handle:
            listing = json.load(handle)
        paths = {}
        for key, short in MAPS:
            paths[short] = os.path.join(STAGE, "%s_%s_2k.jpg" % (asset, short))
            fetch(listing[key]["2k"]["jpg"]["url"], paths[short])

        Image.open(paths["diff"]).convert("RGB").save(os.path.join(STAGE, "T_PBR_%s_A.png" % name))
        normal = Image.open(paths["nor_dx"]).convert("RGB")
        rough = Image.open(paths["rough"]).convert("L").resize(normal.size)
        red, green, _ = normal.split()
        Image.merge("RGB", (red, green, rough)).save(os.path.join(STAGE, "T_PBR_%s_NR.png" % name))
        print("packed", name)
    print("EARTHFALL_PBR_READY")


if __name__ == "__main__":
    main()
