"""Rasterise Natural Earth admin-0 countries into Earthfall's country-ID data.

Source: Natural Earth 1:50m Admin 0 - Countries (public domain), unpacked into
Saved/SourceTextures/natural_earth/ (ne_50m_admin_0_countries.shp/.dbf):
    https://naciscdn.org/naturalearth/50m/cultural/ne_50m_admin_0_countries.zip

Outputs:
  Saved/SourceTextures/T_Earth_CountryID.png  4096x2048 8-bit, value = country ID
                                              (0 = ocean). Imported as a linear,
                                              nearest-filtered, uncompressed texture.
  Content/Earth/Data/CountryID.bin            1024x512 raw bytes, the CPU lookup the
                                              simulation uses for clicks and tallies.
  Source/Earthfall/EarthfallCountryTable.inl  ID -> name, population, grid capacity
                                              and defence readiness.

Grid capacity and defence readiness are synthetic gameplay figures derived from
Natural Earth's population and GDP estimates; they are not real-world data.

No third-party modules beyond Pillow: the .shp and .dbf are parsed by hand.
"""
from pathlib import Path
import math
import struct
import unicodedata

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Saved" / "SourceTextures" / "natural_earth"
STAGE = ROOT / "Saved" / "SourceTextures"
TEXTURE_SIZE = (4096, 2048)
GRID_SIZE = (1024, 512)


def read_dbf(path):
    data = path.read_bytes()
    count, header_size, record_size = struct.unpack("<IHH", data[4:12])
    fields = []
    offset = 32
    while data[offset] != 0x0D:
        name = data[offset:offset + 11].split(b"\x00")[0].decode("ascii")
        fields.append((name, data[offset + 16]))
        offset += 32
    rows = []
    for index in range(count):
        record = data[header_size + index * record_size: header_size + (index + 1) * record_size]
        position = 1
        row = {}
        for name, length in fields:
            row[name] = record[position:position + length].decode("utf-8", "replace").replace(chr(0), " ").strip()
            position += length
        rows.append(row)
    return rows


def read_shp(path):
    data = path.read_bytes()
    shapes = []
    offset = 100
    while offset < len(data):
        (length,) = struct.unpack(">I", data[offset + 4:offset + 8])
        content = data[offset + 8: offset + 8 + length * 2]
        offset += 8 + length * 2
        (kind,) = struct.unpack("<I", content[:4])
        if kind != 5:
            shapes.append(([], (0.0, 0.0, 0.0, 0.0)))
            continue
        box = struct.unpack("<4d", content[4:36])
        part_count, point_count = struct.unpack("<2I", content[36:44])
        parts = list(struct.unpack("<%dI" % part_count, content[44:44 + 4 * part_count])) + [point_count]
        base = 44 + 4 * part_count
        points = struct.unpack("<%dd" % (point_count * 2), content[base:base + 16 * point_count])
        rings = [[(points[2 * i], points[2 * i + 1]) for i in range(parts[p], parts[p + 1])] for p in range(part_count)]
        shapes.append((rings, box))
    return shapes


def ascii_name(text):
    plain = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode("ascii")
    return plain.replace("\\", "").replace('"', "'").strip() or "Unknown"


def to_number(text):
    try:
        return float(text)
    except ValueError:
        return 0.0


def rasterise(countries, size):
    image = Image.new("L", size, 0)
    draw = ImageDraw.Draw(image)
    width, height = size
    # Largest first, so enclaves and island states drawn later are not buried.
    for country in sorted(countries, key=lambda c: -c["area"]):
        for ring in country["rings"]:
            if len(ring) < 3:
                continue
            draw.polygon([((lon + 180.0) / 360.0 * width, (90.0 - lat) / 180.0 * height) for lon, lat in ring],
                         fill=country["id"])
    return image


def main():
    rows = read_dbf(SOURCE / "ne_50m_admin_0_countries.dbf")
    shapes = read_shp(SOURCE / "ne_50m_admin_0_countries.shp")
    if len(rows) != len(shapes):
        raise RuntimeError("dbf/shp record count mismatch: %d vs %d" % (len(rows), len(shapes)))

    countries = []
    for row, (rings, box) in zip(rows, shapes):
        countries.append({
            "name": ascii_name(row.get("NAME_LONG") or row.get("NAME") or row.get("ADMIN", "")),
            "population": max(to_number(row.get("POP_EST", "0")), 0.0),
            "gdp_musd": max(to_number(row.get("GDP_MD", "0")), 0.0),
            "rings": rings,
            "area": (box[2] - box[0]) * (box[3] - box[1]),
        })
    countries.sort(key=lambda c: c["name"])
    if len(countries) > 255:
        raise RuntimeError("More than 255 countries cannot fit an 8-bit ID mask")
    for index, country in enumerate(countries, start=1):
        country["id"] = index

    rasterise(countries, TEXTURE_SIZE).save(STAGE / "T_Earth_CountryID.png")
    grid = rasterise(countries, GRID_SIZE)
    data_dir = ROOT / "Content" / "Earth" / "Data"
    data_dir.mkdir(parents=True, exist_ok=True)
    (data_dir / "CountryID.bin").write_bytes(grid.tobytes())

    lines = [
        "// Generated by Scripts/prepare_country_data.py from Natural Earth 1:50m Admin 0",
        "// Countries (public domain). Do not edit by hand. Index = country ID in",
        "// T_Earth_CountryID and CountryID.bin; ID 0 is open ocean.",
        "// {Name, InitialPopulation, GridCapacityGW, DefenseReadiness 0..100}",
        '{TEXT("International Waters"), 0.0, 0.0f, 0.0f},',
    ]
    for country in countries:
        population = country["population"]
        gdp_per_capita = country["gdp_musd"] * 1.0e6 / population if population > 0 else 0.0
        # Synthetic: richer countries install more generation per head and field more defence.
        grid_gw = population * min(max(gdp_per_capita / 40000.0, 0.02), 1.2) / 1.0e6
        defence = min(max(20.0 + 16.0 * math.log10(max(country["gdp_musd"], 1.0) / 1000.0), 5.0), 98.0)
        lines.append('{TEXT("%s"), %.1f, %.2ff, %.1ff},' % (country["name"], population, grid_gw, defence))
    (ROOT / "Source" / "Earthfall" / "EarthfallCountryTable.inl").write_text("\n".join(lines) + "\n", encoding="ascii")

    covered = sum(1 for value in grid.tobytes() if value) / float(GRID_SIZE[0] * GRID_SIZE[1])
    print("countries=%d land_fraction=%.3f world_population=%.3fB" % (
        len(countries), covered, sum(c["population"] for c in countries) / 1.0e9))
    print("EARTHFALL_COUNTRY_DATA_COMPLETE")


if __name__ == "__main__":
    main()
