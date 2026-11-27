# Earthfall

An Unreal Engine 5.8 C++ prototype for an Earth-scale destruction sandbox. Self-contained: no marketplace plugins.

## What is in the build

**Orbital view**

- 160x320 deformable globe with a lightweight sphere proxy for aiming, 8K NASA Blue Marble day map with NASA bathymetry-shaded oceans, isolated Black Marble city lights and a relief normal map.
- `M_EarthPhotoreal` is **unlit**: sun, soft terminator, GGX ocean sunglint (roughness ~0.30), cloud shadows, city lights, scorch, magma and atmospheric in-scatter are all computed in one shader from a UTC-driven sun vector. The look therefore does not depend on `ASkyAtmosphere`, sky light, exposure metering or shadow cascades. `ASkyAtmosphere` is deliberately not used: at its default 6,360 km scale it buries a 100-unit globe in grey fog.
- `M_EarthAtmosphere` is an additive shell whose glow is derived analytically from the view ray's closest approach to the planet, giving a crisp blue limb that turns orange through the twilight band and browns as the atmosphere is choked with soot.
- `M_SpaceSky` shows ESO's visible-light Milky Way panorama and overlays three brightness tiers of clustered procedural point stars that stay sharp at 4K. `M_EarthSun` is an HDR disc with limb darkening; `M_SunCorona` adds a halo, diffraction spikes and an anamorphic streak. `M_EarthMoon` is a fully procedural cratered Moon on a slow inclined orbit.
- Fixed exposure with the physical-camera term disabled, TSR anti-aliasing, 45 degree field of view.

**Weather, ash and shockwaves**

- Two procedural cloud shells (1.012x and 1.020x radius) from one shared weather function: domain-warped 3D fbm wound into spiral storms by six cyclones, with climate bands for the ITCZ, the dry subtropics and the mid-latitude storm tracks. Clear sky is exactly zero opacity. The shells are lit volumetric-style by marching one step toward the sun, and the surface samples the same function to receive matching soft cloud shadows.
- Up to four live shockwaves are fed to the materials. Each one shoves the weather radially outward, raises a condensation front, lights the ground and cloud deck with the flash, and opens a hole in the clouds that grows behind the front, holds, then heals.
- `AshPlumeRT` is a ping-pong pair advected every 0.1 s by `M_AshAdvection`: soot (R) and radioactive fallout (G) ride a wind field of easterly trades and mid-latitude westerlies, while ground deposition (B) stays where it lands. The surface and cloud deck both render it. A CPU mirror of the same wind field moves matching plumes over the population grid.

**Weapons**

- Six weapons on keys 1-6: kinetic rod, plasma lance, mantle charge, micro black hole, cryo impactor and thermonuclear.
- Every shot launches a visible projectile from the player's orbital point of view: a hull, a glowing head and a camera-facing ribbon trail that flares into a re-entry streak as it meets the atmosphere. The plasma lance is a beam. The strike is resolved when the projectile arrives.
- Three multipliers, adjustable from the keyboard or the on-screen steppers: **Yield** (0.25x-100x), **Blast radius** (0.25x-6x) and **Fallout** (0x-20x). Blast radius also grows with the fourth root of yield.
- Crater dents are capped at 3% of the planet radius and never go below 96% in total, so triangles cannot tear. Scorch, cooling magma veins, a fireball dome, a rising cap, a globe-hugging shock ring and Niagara burst, smoke and spark systems carry the impact, with camera shake scaled by yield and by distance to ground zero.

**Simulation and HUD**

- A seeded 512x256 population and power grid with 46 city and landmark anchors. Lethality compounds with energy, so high yields sterilise the blast zone rather than saturating.
- Drifting plumes cause ongoing fallout casualties and displacement downwind. Airborne soot dims sunlight, cools the climate toward a nuclear-winter equilibrium and degrades the atmosphere; all of it recovers as the soot rains out.
- The HUD is drawn with Slate runtime fonts on a 1080p reference grid, so it is sharp at 4K: planetary status (population, casualties, displaced, fallout deaths, grid blackout, atmosphere, sunlight, temperature, soot, radiation), the arsenal, the multiplier steppers with the resulting blast zone, a target readout with latitude and longitude, and a rotating, scrolling breaking-news ticker.

City locations, population distribution and all casualty, climate, grid and fallout telemetry are synthetic gameplay estimates, not forecasts. The relief normal map is derived from NASA's shaded-relief image and is a visual proxy, not survey-grade elevation.

## Controls

| Input | Action |
| --- | --- |
| RMB drag | Orbit |
| Mouse wheel | Zoom |
| LMB | Launch the selected weapon at the cursor |
| 1-6 | Select weapon (or click a row in the Arsenal panel) |
| Q / E | Yield multiplier down / up |
| A / D | Blast radius multiplier down / up |
| Z / C | Fallout multiplier down / up |
| R | Reset the planet and the simulation |

## Build

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' EarthfallEditor Win64 Development -Project="$PWD\Earthfall.uproject"
```

Then open `Earthfall.uproject` and press Play, or run the uncooked game directly:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\Earthfall.uproject" -game -windowed -ResX=2560 -ResY=1440
```

## Runtime capture

`-EarthfallCapture` spawns `AEarthfallCaptureDirector`, which waits for shaders and textures to finish compiling, flies a fixed shot list, fires real strikes through the same launch path the player uses, writes numbered PNGs to `Saved/Screenshots/Capture/`, logs the measured frame rate and telemetry, and quits. `-EarthfallSunLon` / `-EarthfallSunLat` pin the subsolar point so captures are repeatable.

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\Earthfall.uproject" -game -windowed -ResX=1920 -ResY=1080 -RenderOffscreen -EarthfallCapture -EarthfallSunLon=-60 -EarthfallSunLat=10 -unattended -nosplash -NoSound -log=EarthfallCapture.log
```

## Asset pipeline

The materials are generated, not hand-authored, so their shading is reviewable as text:

- `Scripts/shaders/*.hlsl` holds every material's shading. `common.hlsl` (noise, the weather function, shockwave helpers) is shared.
- `Scripts/validate_shaders.py` compiles each file with `fxc` exactly as it will be wrapped, catching HLSL errors in seconds.
- `Scripts/build_earth_assets.py` imports the textures and builds each material as a few input nodes feeding one Custom node.
- `Scripts/prepare_earth_textures.py` converts the NASA sources into the staged PNGs.

```powershell
& .\Scripts\download_earth_sources.ps1
py -3 -m pip install Pillow numpy
py -3 .\Scripts\prepare_earth_textures.py
py -3 .\Scripts\validate_shaders.py
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\Earthfall.uproject" -run=pythonscript -script="$PWD\Scripts\build_earth_assets.py" -unattended -nop4 -nosplash
```

Set `EARTHFALL_SKIP_TEXTURES=1` to rebuild only the materials, and `EARTHFALL_REIMPORT=T_Space_Skybox` to force named textures through anyway. None of this is needed to run the committed assets. The editor-only PythonScriptPlugin ships with UE 5.8; the runtime game is C++. The Niagara systems under `Content/Earthfall/FX/` are copies of built-in UE 5.8 templates.

## Source imagery

NASA imagery is public-domain; the ESO panorama is licensed CC BY 4.0 and must keep its credit line (ESO/S. Brunier). Originals are downloaded into `Saved/SourceTextures/`, which Git ignores; only the imported assets under `Content/Earth/` are committed.

| Asset | NASA source | Source resolution |
| --- | --- | --- |
| `T_Earth_Day` land | [Blue Marble: Next Generation](https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-map/) | 21600x10800 |
| `T_Earth_Day` ocean, `T_Earth_Normal`, `T_Earth_Masks` | [Blue Marble with Topography and Bathymetry](https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-topography-bathymetry/) | 21600x10800 |
| `T_Earth_Night` | [Earth at Night / Black Marble 2016](https://science.nasa.gov/earth/earth-observatory/earth-at-night/maps/) | 13500x6750 |
| `T_Space_Skybox` | [ESO, The Milky Way panorama (eso0932a)](https://www.eso.org/public/images/eso0932a/). Credit: ESO/S. Brunier, CC BY 4.0 | 6000x3000 |

`prepare_earth_textures.py` grades the data rather than just resizing it: ocean pixels take NASA's bathymetry shading over a deep-navy floor; highlights are soft-capped at 0.72 linear so sunlit snow never clips; Black Marble's moonlit blue land base is removed so only warm city lights remain; and ocean relief is flattened because waves are procedural.

## Known limitations

- Cloud, ash and shockwave effects are shader models on thin shells, not volumetric or fluid simulations.
- Impact-mask and ash stamps are drawn in equirectangular space, so footprints distort near the poles.
- Plasma trenches, steam, seismic propagation and debris gravity are represented by visual feedback and telemetry rather than coupled physics.

## Collaboration

Read `AGENTS.md` and `CLAUDE.md` first. Work on feature branches, build `EarthfallEditor` before opening a pull request, never push directly to `main`, and never commit `Binaries/`, `Intermediate/`, `Saved/` or `DerivedDataCache/`.
