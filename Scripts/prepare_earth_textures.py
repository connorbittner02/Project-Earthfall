"""Convert staged NASA public-domain equirectangular imagery into UE import sources.

Requires Pillow and NumPy in the system Python. Originals and the PNGs written
here stay under Saved/SourceTextures, which is deliberately ignored by Git; only
the imported .uasset files under Content/Earth/Textures are committed.

Outputs (all 8192x4096 unless noted):
  T_Earth_Day.png     Blue Marble land + NASA bathymetry-shaded ocean, highlights
                      soft-capped at 0.72 linear so sunlit snow never clips.
  T_Earth_Night.png   Black Marble with the moonlit blue land base removed, so
                      only warm city lights remain and nothing films the night side.
  T_Earth_Normal.png  Relief normals on land; ocean is flat (waves are procedural).
  T_Earth_Masks.png   R = water, G = relief height proxy, B = snow/ice. 4096x2048.
"""
from pathlib import Path
import hashlib

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
STAGE = ROOT / "Saved" / "SourceTextures"
SIZE = (8192, 4096)
MASK_SIZE = (4096, 2048)
BAND = 512
SNOW_LINEAR_CAP = 0.72
Image.MAX_IMAGE_PIXELS = None

SOURCES = (
    "NASA_BlueMarble_Day.jpg",
    "NASA_BlackMarble_2016_3km.jpg",
    "NASA_BlueMarble_TopoBathy.jpg",
    "NASA_WISE_AllSky_MilkyWay_4096.jpg",
)


def verify_sources():
    for source_name in SOURCES:
        source_path = STAGE / source_name
        if not source_path.is_file() or source_path.stat().st_size <= 500_000:
            raise RuntimeError(f"Missing or undersized source image: {source_path}")
        image = Image.open(source_path)
        image.verify()
        image = Image.open(source_path)
        if image.width < 4096 or image.height < 2048 or abs(image.width / image.height - 2.0) > 0.005:
            raise RuntimeError(f"Source must be a 2:1 image at least 4096x2048: {source_name} {image.size}")
        digest = hashlib.sha256(source_path.read_bytes()).hexdigest()[:16]
        print(f"Verified {source_name}: {image.width}x{image.height}, sha256 {digest}...")


def load_rgb(source_name):
    image = Image.open(STAGE / source_name).convert("RGB")
    if image.size != SIZE:
        image = image.resize(SIZE, Image.Resampling.LANCZOS)
    return np.asarray(image, dtype=np.uint8)


def smoothstep(value, low, high):
    t = np.clip((value - low) / (high - low), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * (c ** (1.0 / 2.4)) - 0.055)


def main():
    verify_sources()
    day_u8 = load_rgb("NASA_BlueMarble_Day.jpg")
    bathy_u8 = load_rgb("NASA_BlueMarble_TopoBathy.jpg")
    night_u8 = load_rgb("NASA_BlackMarble_2016_3km.jpg")

    height, width = SIZE[1], SIZE[0]
    day_out = np.empty((height, width, 3), dtype=np.uint8)
    night_out = np.empty((height, width, 3), dtype=np.uint8)
    water_out = np.empty((height, width), dtype=np.uint8)
    ice_out = np.empty((height, width), dtype=np.uint8)
    relief_out = np.empty((height, width), dtype=np.float32)

    for top in range(0, height, BAND):
        rows = slice(top, min(top + BAND, height))
        day = day_u8[rows].astype(np.float32) / 255.0
        bathy = bathy_u8[rows].astype(np.float32) / 255.0
        night = night_u8[rows].astype(np.float32) / 255.0

        # Water: dark blue-dominant pixels in the natural-colour mosaic, widened
        # by the bathymetry map so bright shelf seas (Gulf of Mexico, Java Sea)
        # are classified as water instead of rough land.
        red, green, blue = day[..., 0], day[..., 1], day[..., 2]
        luma = 0.2126 * red + 0.7152 * green + 0.0722 * blue
        day_water = smoothstep(blue - np.maximum(red, green), 0.015, 0.060) * (1.0 - smoothstep(luma, 0.10, 0.28))
        bathy_excess = bathy[..., 2] - np.maximum(bathy[..., 0], bathy[..., 1])
        bathy_water = smoothstep(bathy_excess, 0.045, 0.11) * (1.0 - smoothstep(luma, 0.30, 0.50))
        water = np.maximum(day_water, bathy_water)

        # Snow and ice: bright and unsaturated.
        ice = smoothstep(np.min(day, axis=2), 0.55, 0.80) * (1.0 - water)

        # Ocean colour comes from NASA's bathymetry shading over a deep-navy floor,
        # so abyssal plains stay rich blue instead of crushing to black.
        day_linear = srgb_to_linear(day)
        ocean_linear = np.array([0.006, 0.022, 0.070], dtype=np.float32) + srgb_to_linear(bathy) * 1.35
        graded = day_linear * (1.0 - water[..., None]) + ocean_linear * water[..., None]

        # Soft-knee highlight cap: sunlit snow tops out at 0.72 linear albedo.
        peak = np.max(graded, axis=2)
        knee = 0.50
        span = SNOW_LINEAR_CAP - knee
        compressed = np.where(peak > knee, knee + span * (1.0 - np.exp(-(peak - knee) / span)), peak)
        graded *= (compressed / np.maximum(peak, 1e-5))[..., None]
        day_out[rows] = np.clip(linear_to_srgb(graded) * 255.0 + 0.5, 0, 255).astype(np.uint8)

        # City lights: Black Marble paints moonlit land and ice a cool blue. Real
        # lights are warm, so gate on red-minus-blue and drop everything else.
        warmth = night[..., 0] - night[..., 2]
        lights = night * smoothstep(warmth, -0.035, 0.085)[..., None]
        night_out[rows] = np.clip(lights * 1.5 * 255.0 + 0.5, 0, 255).astype(np.uint8)

        water_out[rows] = np.clip(water * 255.0 + 0.5, 0, 255).astype(np.uint8)
        ice_out[rows] = np.clip(ice * 255.0 + 0.5, 0, 255).astype(np.uint8)
        relief_out[rows] = (0.2126 * bathy[..., 0] + 0.7152 * bathy[..., 1] + 0.0722 * bathy[..., 2]) * (1.0 - water)
        print(f"  graded rows {rows.start}-{rows.stop}")

    del day_u8, bathy_u8, night_u8
    Image.fromarray(day_out, "RGB").save(STAGE / "T_Earth_Day.png")
    Image.fromarray(night_out, "RGB").save(STAGE / "T_Earth_Night.png")
    print("T_Earth_Day.png, T_Earth_Night.png written")
    del day_out, night_out

    # The relief source is NASA's shaded topography image: its luminance is a
    # visual height proxy, not survey-grade elevation. Ocean was zeroed above.
    relief_image = Image.fromarray(np.clip(relief_out * 255.0, 0, 255).astype(np.uint8), "L")
    relief_image = relief_image.filter(ImageFilter.GaussianBlur(0.8))
    h = np.asarray(relief_image, dtype=np.float32) / 255.0
    strength = 3.5
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * strength
    dy[0, :] = 0.0
    dy[-1, :] = 0.0
    land = 1.0 - (water_out.astype(np.float32) / 255.0)
    dx *= land
    dy *= land
    length = np.sqrt(dx * dx + dy * dy + 1.0)
    # Tangent frame: +X east (increasing U), +Y south (increasing V), +Z up.
    normal = np.stack(((-dx / length) * 0.5 + 0.5, (-dy / length) * 0.5 + 0.5, (1.0 / length) * 0.5 + 0.5), axis=-1)
    Image.fromarray(np.clip(normal * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGB").save(STAGE / "T_Earth_Normal.png")
    print("T_Earth_Normal.png written")
    del normal, dx, dy, length

    masks = np.stack((water_out, np.asarray(relief_image, dtype=np.uint8), ice_out), axis=-1)
    Image.fromarray(masks, "RGB").resize(MASK_SIZE, Image.Resampling.LANCZOS).save(STAGE / "T_Earth_Masks.png")
    print("T_Earth_Masks.png written (R water, G relief, B ice)")
    print("EARTHFALL_TEXTURE_PREP_COMPLETE")


if __name__ == "__main__":
    main()
