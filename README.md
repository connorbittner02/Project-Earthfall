# Earthfall

An Unreal Engine 5.8 prototype for an Earth-scale destruction sandbox.

## Current prototype

- 128x256 spherical globe mesh using imported 8192x4096 NASA Blue Marble day, Black Marble night-lights, Blue Marble clouds, relief-derived normal, and starfield textures
- Lit Earth material with a sun-driven day/night terminator, coastline city lights, ocean/land roughness, a blue Fresnel limb, and a dynamic impact-mask render target
- Separate translucent cloud shell, atmosphere shell, and inverted starfield backdrop
- 2048x1024 impact render target with cumulative scorch, cooling emissive heat, city-light blackout, and radial cloud clearing stamps
- Orbit camera with RMB-drag mouse look and wheel zoom; visible cursor aims directly at the surface
- Six selectable strike types with differentiated blast radii, energy, cooldown, color, and feedback, including a wide-EMP thermonuclear strike
- Click-to-strike cumulative craters with raised rims, biome-aware scorch, cooling heat glow, and a surface blast-radius preview
- Distinct kinetic, plasma, mantle, micro black hole, and cryo impact responses; expanding shockwaves reuse one mesh
- Runtime atmosphere and cloud shells with texture-based radial cloud clearing and gradual recovery
- A seeded 512x256 population and power grid plus 45 city/landmark anchors, casualty/displacement/blackout estimates, city-light extinction, global atmosphere/temperature/debris telemetry, and a breaking-news ticker
- On-screen aiming, target lock, weapon selection, cooldown, impact counter, and full simulation reset

The rendered surface now uses real NASA Blue Marble and Black Marble imagery. The 8K normal map is derived from NASA's relief-shaded topography/bathymetry image and is a visual height proxy, not survey-grade elevation. City locations, population distribution, and casualty/climate/grid telemetry are gameplay estimates, not real-world forecasts. Craters deform the CPU mesh; this is not a GPU SDF or geophysical model.

## NASA source imagery and import

NASA imagery is public-domain source data. The original files are downloaded into `Saved/SourceTextures/`, which is ignored by Git. The imported and processed UE assets live in `Content/Earth/` and are committed.

| Texture | NASA source | Source resolution |
| --- | --- | --- |
| Day albedo | [Blue Marble: Next Generation, October 2004](https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-map/) | 21600x10800 |
| City lights | [Earth at Night / Black Marble 2016](https://science.nasa.gov/earth/earth-observatory/earth-at-night/maps/) | 13500x6750 |
| Clouds | [Blue Marble: Clouds](https://visibleearth.nasa.gov/images/57747/blue-marble-clouds) | 8192x4096 |
| Relief source | [Blue Marble with Topography and Bathymetry](https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-topography-bathymetry/) | 21600x10800 |

The legacy NASA `eoimages.gsfc.nasa.gov` host did not respond from the development PC. The cloud TIFF was fetched from a public mirror and verified byte-for-byte against its NASA-source SHA-256 (`d137775d8966ab8d443fd5126dc6e7ad72072bc1ed50555c5818d221735daf0f`). No third-party asset is redistributed as a gameplay dependency.

To repeat the source preparation and asset import:

```powershell
& .\Scripts\download_earth_sources.ps1
py -3 -m pip install Pillow numpy
py -3 .\Scripts\prepare_earth_textures.py
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\Earthfall.uproject" -run=pythonscript -script="$PWD\Scripts\build_earth_assets.py" -unattended -nop4 -nosplash
```

The editor-only PythonScriptPlugin is built into UE 5.8 and is enabled for asset authoring; the runtime game is C++ and has no marketplace plugin dependency. The Python preparation scripts are not needed to run the committed assets.

## Run

1. Install Unreal Engine 5.8 with its C++ toolchain.
2. Open Earthfall.uproject.
3. Allow Unreal to build the Earthfall module.
4. Press Play. Hold RMB and drag to orbit, use the wheel to zoom toward the cloud layer, press 1-6 to select a weapon, click the planet to strike, and press R to restore the terrain, impact mask, population grid, city lights, telemetry, and news ticker.

Earth surface, cloud, atmosphere, sky, and shockwave materials and textures are included under `Content/Earth/` and `Content/Earthfall/Materials`; no manual material setup is required.

## Build from PowerShell

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' EarthfallEditor Win64 Development -Project="$PWD\Earthfall.uproject"
```

## Known limitations

- The normal map is derived from a shaded relief visualization; it is an artistic normal source and not a true elevation DEM.
- Population distribution, city anchors, casualty estimates, fallout, and global climate telemetry are synthetic gameplay data.
- Plasma trenches, steam, seismic fault propagation, debris gravity, and climate response are represented by lightweight visual feedback/telemetry rather than coupled physical simulations.
- The thermonuclear option uses a stylized double-flash, thermal plume, extended cloud-clear/EMP stamp, and synthetic fallout telemetry rather than a physical radiation model.

## Playable demo package

The Windows demo build is archived to the separate outputs/EarthfallDemo folder. Launch Earthfall.exe in its Windows subfolder. The package targets Windows 64-bit Development and includes the project's cooked content.

## Collaboration with Claude and Gemini

- Give Claude Code access to this GitHub repository and ask it to read AGENTS.md and CLAUDE.md.
- Share the private repository link with Gemini and grant its GitHub integration access if available. Otherwise use a sanitized export or patch; a URL alone does not grant access.
- Use separate feature branches and pull requests so all three assistants can review and contribute without overwriting each other's work.
