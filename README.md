# Earthfall

An Unreal Engine 5.8 prototype for an Earth-scale destruction sandbox.

## Current prototype

- Procedurally generated 2048x1024 spherical surface texture and 128x256 globe mesh with smooth multi-octave land, bathymetry, ridge, and polar masks
- Orbit camera with RMB-drag mouse look and wheel zoom; visible cursor aims directly at the surface
- Five selectable strike types with differentiated blast radii, energy, cooldown, color, and feedback
- Click-to-strike cumulative craters with raised rims, biome-aware scorch, cooling heat glow, and a surface blast-radius preview
- Distinct kinetic, plasma, mantle, micro black hole, and cryo impact responses; expanding shockwaves reuse one mesh
- Runtime atmosphere and cloud shells with radial cloud clearing and gradual recovery
- A seeded 512x256 population and power grid plus 45 city/landmark anchors, casualty/displacement/blackout estimates, city-light extinction, global atmosphere/temperature/debris telemetry, and a breaking-news ticker
- On-screen aiming, target lock, weapon selection, cooldown, impact counter, and full simulation reset

This is a self-contained C++ prototype, not a data-validated photorealistic Earth simulator. The terrain, city-light mask, and population grid are generated procedurally; casualty and climate figures are gameplay estimates, not real-world forecasts. Craters deform the CPU mesh, and the surface still uses the included vertex-color material rather than authored satellite albedo/normal/roughness maps or a GPU SDF terrain system.

## Run

1. Install Unreal Engine 5.8 with its C++ toolchain.
2. Open Earthfall.uproject.
3. Allow Unreal to build the Earthfall module.
4. Press Play. Hold RMB and drag to orbit, use the wheel to zoom, press 1-5 to select a weapon, click the planet to strike, and press R to restore the terrain, population grid, city lights, telemetry, and news ticker.

The runtime surface and shockwave materials are included under `Content/Earthfall/Materials`; no marketplace plugins or additional in-editor material setup are required. The generated texture is bilinear-filtered and transient, so it is rebuilt from code at runtime and is not committed as an asset.

## Build from PowerShell

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' EarthfallEditor Win64 Development -Project="$PWD\Earthfall.uproject"
```

## Known limitations

- The material asset does not expose separate ocean/land roughness controls, and generated terrain is not based on real satellite data.
- Cloud clearing is a visual procedural mask, while plasma trenches, steam, seismic fault propagation, debris gravity, and global climate are represented by lightweight feedback/telemetry rather than coupled physical simulations.
- Population distribution and all strike impact figures are synthetic. Use the telemetry for gameplay feedback only.

## Playable demo package

The Windows demo build is archived to the separate outputs/EarthfallDemo folder. Launch Earthfall.exe in its Windows subfolder. The package targets Windows 64-bit Development and includes the project's cooked content.

## Collaboration with Claude and Gemini

- Give Claude Code access to this GitHub repository and ask it to read AGENTS.md and CLAUDE.md.
- Share the private repository link with Gemini and grant its GitHub integration access if available. Otherwise use a sanitized export or patch; a URL alone does not grant access.
- Use separate feature branches and pull requests so all three assistants can review and contribute without overwriting each other's work.
