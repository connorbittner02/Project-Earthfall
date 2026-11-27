# Earthfall

An Unreal Engine 5.8 C++ prototype for an Earth-scale destruction sandbox. Self-contained: no marketplace plugins.

## What is in the build

**Orbital view**

- 160x320 deformable globe with a lightweight sphere proxy for aiming, 8K NASA Blue Marble day map with NASA bathymetry-shaded oceans, isolated Black Marble city lights and a relief normal map.
- `M_EarthPhotoreal` is **unlit**: sun, soft terminator, GGX ocean sunglint (roughness ~0.30), cloud shadows, city lights, scorch, magma and atmospheric in-scatter are all computed in one shader from a UTC-driven sun vector. The look therefore does not depend on `ASkyAtmosphere`, sky light, exposure metering or shadow cascades. `ASkyAtmosphere` is deliberately not used: at its default 6,360 km scale it buries a 100-unit globe in grey fog.
- `M_EarthAtmosphere` is an additive shell whose glow is derived analytically from the view ray's closest approach to the planet, giving a crisp blue limb that turns orange through the twilight band and browns as the atmosphere is choked with soot.
- `M_SpaceSky` shows ESO's visible-light Milky Way panorama and overlays three brightness tiers of clustered procedural point stars that stay sharp at 4K. `M_EarthSun` is an HDR disc with limb darkening; `M_SunCorona` adds a halo, diffraction spikes and an anamorphic streak. `M_EarthMoon` is a fully procedural cratered Moon on a slow inclined orbit.
- Fixed exposure with the physical-camera term disabled, TSR anti-aliasing at 100% screen percentage (native resolution), 45 degree field of view.

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

**Low orbit**

- Minimum orbit is 1.035 R, above the cloud decks. Zoom eases in log-altitude, drag speed scales with altitude, and below 0.22 R of altitude the camera pitches up to a horizon view.
- Distance-faded procedural detail: micro-terrain normals, mottled ground, forest canopy, city blocks, point city lights, extra ocean wave trains in the sunglint, and finer cloud erosion. The atmosphere shell is two-sided so the limb survives the camera entering it.

**Movie-grade destruction** (see `docs/movie-grade-destruction.md`)

- Impacts have a physical body: diameter from the blast multiplier, entry speed from yield, energy from `1/2 m v^2`. Ocean strikes size their tsunami from the water they displace.
- Ocean hits throw a splash crown and a steam column; land hits raise an ash column to 1.08 R that shadows the clouds and ground.
- Sub-orbital ejecta fly ballistic arcs under spherical gravity and fall back as re-entry streaks that start secondary fires.
- Airborne soot turns the limb and the terrain haze amber-red and dims the sun.

**Sandbox** (see `docs/worldbox-sandbox.md`)

- A three-tab command dock: orbital weapons, bio and nature powers, diplomacy and spawners.
- Fourteen AI powers with alliances, wars, missile exchanges, interceptors and a Kessler debris ring.
- Plague, rage, cure and insurgency zones; air and sea traffic; nuclear winter; synthesised audio.

**Munitions, cities and orders** (see `docs/civ-munitions-growth.md`)

- Five shells (Standard, MIRV Cluster, Incendiary, EMP, Bunker-Buster), chosen with F.
- Cities grow in peace and their lights sprawl; hubs climb from CITY to MEGACITY to ARCOLOGY.
- A CIV tab: resource and tech drops through four tech tiers, ground armies, and direct orders to a commanded nation.

## Controls

| Input | Action |
| --- | --- |
| RMB drag | Orbit |
| Mouse wheel | Zoom, eased, down to 1.035 planet radii; the camera tilts to the horizon in low orbit |
| LMB | Launch the selected weapon at the cursor |
| Tab | Cycle the command dock's tabs |
| F | Cycle the loaded shell (or click the SHELL strip) |
| 1-6 | Select a tool on the open tab (or click a row) |
| Q / E | Yield multiplier down / up |
| A / D | Blast radius multiplier down / up |
| Z / C | Fallout multiplier down / up |
| R | Reset the planet and the simulation |
| H | Cycle HUD: full, ticker only, hidden (or click the eye button) |
| M / MMB | Inspect mode: click a nation for its Country Dossier |

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
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\Earthfall.uproject" -game -windowed -ForceRes -enablehighdpi -NoVSync -WinX=0 -WinY=0 -ResX=3840 -ResY=2160 -EarthfallCapture -EarthfallSunLon=-60 -EarthfallSunLat=10 -unattended -nosplash -NoSound -log=EarthfallCapture.log
```

Use a real window for anything you intend to measure. `-RenderOffscreen` is handy for a quick headless smoke test, but it ignores `-ResX`/`-ResY` and renders at roughly 888x500, so its frame rate and sharpness say nothing about 4K. `-fullscreen` follows the primary display's mode and is vsync-capped.

Measured on an RTX 4080 at native 3840x2160, TSR at 100% screen percentage, vsync off, across the whole scripted run including every strike: about 68 fps average after the tsunami, wildfire, crater and low-orbit passes. The cloud shells are the dominant cost. `Media/Capture/` holds the 888x500 smoke-test set and `Media/Capture4K/` holds native 4K frames.

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

- Low orbit is procedural detail over 8K maps (about 5 km per texel), not streamed terrain. Cesium for Unreal 2.31.0 was verified to compile and load on UE 5.8 but needs a Cesium ion token to stream anything; see `docs/30k-ft-cesium-plan.md` for the test results, manual install steps and integration design.
- Cloud, ash and shockwave effects are shader models on thin shells, not volumetric or fluid simulations.
- Impact-mask and ash stamps are drawn in equirectangular space, so footprints distort near the poles.
- Plasma trenches, steam, seismic propagation and debris gravity are represented by visual feedback and telemetry rather than coupled physics.

## Collaboration

Read `AGENTS.md` and `CLAUDE.md` first. Work on feature branches, build `EarthfallEditor` before opening a pull request, never push directly to `main`, and never commit `Binaries/`, `Intermediate/`, `Saved/` or `DerivedDataCache/`.
