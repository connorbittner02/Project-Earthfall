"""Convert staged NASA public-domain equirectangular source imagery for UE import.

Requires Pillow and NumPy in the system Python. The originals stay under
Saved/SourceTextures and are deliberately ignored by Git.
"""
from pathlib import Path
import random
import hashlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
STAGE = ROOT / "Saved" / "SourceTextures"
SIZE = (8192, 4096)
Image.MAX_IMAGE_PIXELS = None

SOURCES = (
    "NASA_BlueMarble_Day.jpg",
    "NASA_BlackMarble_2016_3km.jpg",
    "NASA_BlueMarble_Clouds_8192.tif",
    "NASA_BlueMarble_TopoBathy.jpg",
)
for source_name in SOURCES:
    source_path = STAGE / source_name
    if not source_path.is_file() or source_path.stat().st_size <= 500_000:
        raise RuntimeError(f"Missing or undersized source image: {source_path}")
    image = Image.open(source_path)
    image.verify()
    image = Image.open(source_path)
    if image.width < 4096 or image.height < 2048 or abs(image.width / image.height - 2.0) > 0.005:
        raise RuntimeError(f"Source must be a 2:1 image at least 4096x2048: {source_name} {image.size}")
    print(f"Verified {source_name}: {image.width}x{image.height}, {source_path.stat().st_size:,} bytes")

cloud_hash = hashlib.sha256((STAGE / "NASA_BlueMarble_Clouds_8192.tif").read_bytes()).hexdigest()
if cloud_hash != "d137775d8966ab8d443fd5126dc6e7ad72072bc1ed50555c5818d221735daf0f":
    raise RuntimeError(f"NASA Blue Marble Clouds source checksum mismatch: {cloud_hash}")


def resize_source(source_name: str, target_name: str, mode: str) -> None:
    image = Image.open(STAGE / source_name).convert(mode)
    if image.size != SIZE:
        image = image.resize(SIZE, Image.Resampling.LANCZOS)
    image.save(STAGE / target_name, optimize=True)
    print(f"{target_name}: {image.width}x{image.height}")


resize_source("NASA_BlueMarble_Day.jpg", "T_Earth_Day.png", "RGB")
resize_source("NASA_BlackMarble_2016_3km.jpg", "T_Earth_Night.png", "RGB")
resize_source("NASA_BlueMarble_Clouds_8192.tif", "T_Earth_Clouds.png", "L")

# The topography/bathymetry source is NASA's relief-shaded Blue Marble map.
# Its luminance is used as a visual height proxy, not as survey-grade elevation.
relief = Image.open(STAGE / "NASA_BlueMarble_TopoBathy.jpg").convert("RGB")
relief = relief.resize(SIZE, Image.Resampling.LANCZOS)
height = relief.convert("L").filter(ImageFilter.GaussianBlur(0.6))
height.save(STAGE / "T_Earth_Height.png", optimize=True)

h = np.asarray(height, dtype=np.float32) / 255.0
dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 2.5
dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 2.5
nx, ny, nz = -dx, -dy, np.ones_like(h)
length = np.sqrt(nx * nx + ny * ny + nz * nz)
normal = np.stack((nx / length * 0.5 + 0.5, ny / length * 0.5 + 0.5,
                   nz / length * 0.5 + 0.5), axis=-1)
Image.fromarray(np.clip(normal * 255, 0, 255).astype(np.uint8), "RGB").save(
    STAGE / "T_Earth_Normal.png", optimize=True)

# A deterministic, restrained starfield with a low-contrast galactic band.
width, height_px = SIZE
u = np.arange(width, dtype=np.float32)[None, :] / width
v = np.arange(height_px, dtype=np.float32)[:, None] / height_px
band = 0.5 + 0.19 * np.sin((u - 0.08) * np.pi * 2.0)
falloff = np.exp(-((v - band) / 0.09) ** 2) * 4.0
rng = np.random.default_rng(83417)
noise = rng.random((height_px, width), dtype=np.float32)
base = np.clip(1.2 + falloff * (0.45 + noise * 0.8), 0, 10).astype(np.uint8)
space = np.stack((base, base, np.clip(base.astype(np.int16) + 3, 0, 255).astype(np.uint8)), axis=-1)
space_image = Image.fromarray(space, "RGB")
draw = ImageDraw.Draw(space_image)
random.seed(83417)
for _ in range(16000):
    sx, sy = random.randrange(width), random.randrange(height_px)
    brightness = random.choices((60, 95, 145, 205, 255), (68, 20, 8, 3, 1))[0]
    color = (brightness, int(brightness * random.uniform(0.84, 1.0)),
             min(255, int(brightness * random.uniform(0.82, 1.08))))
    draw.point((sx, sy), fill=color)
    if brightness > 200:
        draw.point((sx + 1, sy), fill=tuple(c // 3 for c in color))
space_image.save(STAGE / "T_Earth_Starfield.png", optimize=True)

print("Prepared day, night, cloud, height, normal and starfield maps in Saved/SourceTextures.")
