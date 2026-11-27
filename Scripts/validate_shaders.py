"""Compile every Scripts/shaders/*.hlsl material body with fxc as a fast pre-check.

Unreal only reports HLSL errors after an asset rebuild and an asynchronous shader
compile. This wraps each file exactly as build_earth_assets.py does (common.hlsl
plus the material file inside one struct), calls Main with dummy arguments, and
compiles it for ps_5_0, the profile the D3D11/SM5 runtime uses.

    py -3 Scripts/validate_shaders.py
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

SHADERS = Path(__file__).resolve().parent / "shaders"
FXC_CANDIDATES = sorted(Path(r"C:\Program Files (x86)\Windows Kits\10\bin").glob("10.*/x64/fxc.exe"), reverse=True)


def main_signature(source):
    match = re.search(r"\b(float[234]?)\s+Main\s*\(([^)]*)\)", source, re.S)
    if not match:
        raise RuntimeError("No Main(...) found")
    params = []
    for raw in match.group(2).split(","):
        kind, name = raw.split()
        params.append((kind, name))
    return match.group(1), params


def wrap(common, body):
    return_type, params = main_signature(body)
    declarations, arguments = [], []
    for kind, name in params:
        if kind in ("Texture2D", "SamplerState"):
            declarations.append(f"{kind} g{name};")
            arguments.append(f"g{name}")
        else:
            arguments.append(f"({kind})(Seed.x * 0.37 + 0.21)")
    return "\n".join([
        "#define Texture2DSampleLevel(T, S, UV, L) T.SampleLevel(S, UV, L)",
        "#define Texture2DSample(T, S, UV) T.Sample(S, UV)",
        "float4 Seed;",
        *declarations,
        "struct FEarthfall {",
        common,
        body,
        "};",
        "float4 main(float4 Position : SV_Position) : SV_Target",
        "{",
        "    FEarthfall Earthfall;",
        f"    {return_type} result = Earthfall.Main({', '.join(arguments)});",
        "    return float4(result" + {"float": ", 0, 0, 1", "float2": ", 0, 1", "float3": ", 1", "float4": ""}[return_type] + ");",
        "}",
    ])


def main():
    if not FXC_CANDIDATES:
        print("fxc.exe not found in the Windows SDK; skipping shader validation.")
        return 0
    fxc = FXC_CANDIDATES[0]
    common = (SHADERS / "common.hlsl").read_text(encoding="utf-8")
    failures = 0
    with tempfile.TemporaryDirectory() as scratch:
        for path in sorted(SHADERS.glob("*.hlsl")):
            if path.name == "common.hlsl":
                continue
            wrapped = Path(scratch) / path.name
            wrapped.write_text(wrap(common, path.read_text(encoding="utf-8")), encoding="utf-8")
            result = subprocess.run([str(fxc), "/nologo", "/T", "ps_5_0", "/E", "main", "/Fo", str(wrapped) + ".o", str(wrapped)],
                                    capture_output=True, text=True)
            if result.returncode == 0:
                print(f"ok      {path.name}")
            else:
                failures += 1
                print(f"FAILED  {path.name}\n{result.stdout}{result.stderr}")
    print("EARTHFALL_SHADERS_VALID" if failures == 0 else f"{failures} shader(s) failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
