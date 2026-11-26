# Earthfall

An Unreal Engine 5.8 prototype for an Earth-scale destruction sandbox.

## Current prototype

- Procedural Unreal sphere as the initial planet placeholder
- Orbit camera with mouse look and wheel zoom
- Click-to-strike interaction with a flash and expanding shockwave
- C++ impact API ready for later terrain and simulation systems

The current sphere does not yet deform. The design brief describes a much larger target; this repository starts with a compilable gameplay foundation.

## Run

1. Install Unreal Engine 5.8 with its C++ toolchain.
2. Open Earthfall.uproject.
3. Allow Unreal to build the Earthfall module.
4. Press Play. Drag the mouse to orbit, use the wheel to zoom, and click the planet to strike.

## Build from PowerShell

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' EarthfallEditor Win64 Development -Project="$PWD\Earthfall.uproject"
```

## Roadmap

1. **Polish the orbital prototype:** reliable input mapping, impact tuning, and a clear development HUD.
2. **Planet presentation:** texture/material pipeline, day/night lighting, atmosphere, and cloud shell.
3. **Localized impact simulation:** GPU impact masks and chunked spherical terrain representation.
4. **Gameplay systems:** weapon definitions, event telemetry, and data-driven strike effects.
5. **World data:** optional licensed/open demographic and night-light layers with documented provenance.
6. **Scale and performance:** profiling, quality tiers, and platform targets.

Runtime GPU SDF carving, live casualty estimates, and photorealistic data layers are not implemented yet. They require a dedicated rendering/data design and measurable performance targets before adoption.

## Collaboration with Claude and Gemini

- Give Claude Code access to this GitHub repository and ask it to read AGENTS.md and CLAUDE.md.
- Share the private repository link with Gemini and grant its GitHub integration access if available. Otherwise use a sanitized export or patch; a URL alone does not grant access.
- Use separate feature branches and pull requests so all three assistants can review and contribute without overwriting each other's work.
