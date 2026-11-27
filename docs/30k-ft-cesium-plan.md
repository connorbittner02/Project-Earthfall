# Low altitude: what ships now, and the Cesium plan

## What was tested (2026-11-27, headless)

Cesium for Unreal **v2.31.0** (Apache 2.0, github.com/CesiumGS/cesium-unreal) publishes a
UE 5.8 build. On this machine, with no Epic Launcher and no GUI:

| Step | Result |
| --- | --- |
| Download `CesiumForUnreal-58-v2.31.0.zip` (1.26 GB) from GitHub Releases | OK |
| Extract to `Plugins/CesiumForUnreal`, enable in `Earthfall.uproject` | OK |
| `Build.bat EarthfallEditor Win64 Development` with the shipped intermediates | **Fails**: UBT recompiles the plugin and its precompiled headers do not match (`C2855`) |
| Delete `Plugins/CesiumForUnreal/Intermediate`, build again | **Succeeds**: 185 actions, about 4 minutes |
| Launch `UnrealEditor-Cmd` and query Python | Plugin mounts; `Cesium3DTileset`, `CesiumGeoreference`, `CesiumIonServer` exist |

So the plugin installs, compiles and loads headlessly on UE 5.8. It was **not** left enabled,
for two reasons:

1. **It streams nothing without a Cesium ion access token.** Cesium World Terrain, Bing
   imagery and Google Photorealistic 3D Tiles are all served through ion. The token comes
   from a free cesium.com account, and creating that account is a manual step for the
   project owner. Without it the plugin adds startup time and no pixels.
2. **A project file that references a 5.6 GB untracked plugin breaks every clean clone.**

The project file was restored, the generated `Content/CesiumSettings/` was removed, and the
compiled plugin was parked at `Saved/SourceTextures/cesium/CesiumForUnreal/` (git-ignored).

## Manual install steps

1. Create a free account at https://ion.cesium.com and copy the default access token
   (Access Tokens page). Add the "Cesium World Terrain" and "Bing Maps Aerial" assets to
   "My Assets" if they are not there already.
2. Restore the plugin. Either move the parked copy back:
   ```powershell
   New-Item -ItemType Directory -Force Plugins | Out-Null; Move-Item Saved\SourceTextures\cesium\CesiumForUnreal Plugins\CesiumForUnreal
   ```
   or download `CesiumForUnreal-58-v2.31.0.zip` again, extract it to `Plugins/`, and
   delete `Plugins/CesiumForUnreal/Intermediate` before building.
3. Add to the `Plugins` array of `Earthfall.uproject`:
   ```json
   { "Name": "CesiumForUnreal", "Enabled": true, "Optional": true }
   ```
   `Optional` keeps clones without the plugin opening normally.
4. Add `Plugins/CesiumForUnreal/` to `.gitignore`. Do not commit the plugin.
5. Build `EarthfallEditor Win64 Development`.
6. Open the editor, Cesium panel, **Connect to Cesium ion**, sign in, and paste or select
   the token as the project default. This writes `Content/CesiumSettings/`; the token inside
   is a credential, so decide deliberately whether that folder is committed.
7. Record the plugin in `AGENTS.md` under the plugin policy.

## Integration design (not built yet)

The Cesium globe is true WGS84 scale (radius 6,378 km, about 6.4e8 Unreal units). The
Earthfall globe is 100 units, and every simulation (impact mask, ash, tsunami, fire,
craters, population) lives in equirectangular render targets on that sphere. The two should
not be merged; they should hand over.

- Keep the orbital globe and the simulation core exactly as they are.
- Add a `Cesium3DTileset` (World Terrain + Bing imagery) with a `CesiumGeoreference`, scaled
  by `PlanetRadius / 6378137 m` and centred on the planet, hidden above about 1.05 R.
- Cross-fade between 1.10 R and 1.035 R: fade the tileset in and the `M_EarthPhotoreal`
  albedo out, while the cloud shells, atmosphere shell and all strike effects keep drawing.
- Destruction on tiles: give the tileset a material layer that samples the same render
  targets by longitude/latitude (convert ECEF with the same `atan2(-Y, X)` convention used
  everywhere else).
- Longitude sign: Cesium is right-handed ECEF, Unreal is left-handed with east = -Y. Verify
  with one landmark before trusting any alignment.
- Budget: tiles stream over the network, so capture runs need a warm-up wait on the tileset
  load progress reaching 100 before each low-altitude shot.

## What ships instead (Phase C fallback)

- Minimum orbit lowered from 1.06 R to **1.035 R**, above the high cloud deck at 1.02 R.
- Zoom and orbit ease toward a goal: zoom in log-altitude, drag speed proportional to
  altitude, so the ground moves under the cursor at the same screen rate at any height.
- Below 0.22 R of altitude the camera pitches up from nadir to 70 degrees, ending on a
  horizon view with the limb a third of the way down the frame.
- `M_EarthAtmosphere` is two-sided and culls analytically, so the limb glow survives the
  camera entering the shell.
- Distance-faded procedural detail in `earth_surface.hlsl`: micro-terrain normals scaled by
  the NASA relief, mottled ground albedo, forest canopy clumps, city block structure, city
  lights broken into points, and three extra ocean wave trains that sparkle in the sunglint.
- A finer cloud erosion octave near the camera, and a horizon fade on the cloud shells.

## Known limits of the fallback

- The base maps are 8K (about 5 km per texel). At 1.035 R one texel covers roughly 100
  screen pixels at 4K, so coastlines are soft and block compression shows. Procedural detail
  hides this; it does not add real geography. Only streamed tiles fix it.
- 1.035 R is about 220 km at true scale, not 30,000 ft. The cloud decks sit at exaggerated
  heights (1.2% and 2% of the radius), which is why the camera cannot go lower.
- Night city lights at lowest orbit still show the source texel grid.
- Faint straight slivers can appear in thin cloud right at the horizon of a cloud shell,
  where the 160x320 shell mesh shows its facets.

## Status on `feature/cesium-30k-ft`

Built, and verified with a Cesium ion token at 3840x2160 (see the end of this file).

- `Plugins/CesiumForUnreal/` restored and git-ignored; enabled as `"Optional": true`.
  `Earthfall.Build.cs` defines `EARTHFALL_WITH_CESIUM` when the folder exists.
- `AEarthfallTerrainStreamer` spawns a `CesiumGeoreference` (true origin, scale
  `0.9985 * PlanetRadius / 6378137`) and a Cesium World Terrain tileset with Bing aerial
  imagery. The ellipsoid sits 0.15% under the planet sphere so every tile, Everest
  included, is hidden beneath the globe until the hand-over.
- Hand-over between 1.10 R and 1.035 R: `M_EarthPhotoreal` is now masked and dissolves
  through a temporal dither (`surface_fade.hlsl`) onto the tiles.
- The orbit floor drops to 30,000 ft (9,144 m, 0.143 units) above the ellipsoid, with a
  closer near plane, finer zoom steps, extra horizon tilt and cloud decks that thin out
  as the camera passes them.
- Destruction on tiles uses a planet-sized deferred decal (`M_TerrainDestruction`,
  `terrain_decal.hlsl`) instead of replacing the tileset material. It converts each pixel
  to longitude/latitude and samples ImpactMaskRT, CraterRT, FireMaskRT, TsunamiRT and
  AshPlumeRT. Cesium drives its own layered material for imagery; a decal leaves it stock.
- Everything is gated on tiles actually being loaded. Without the plugin or a token the
  capture run is pixel-equivalent to Phase C (verified at 3840x2160, 26 shots).

## Verified with a token (2026-11-27, 3840x2160, 29 shots, about 76 fps)

| Check | Result |
| --- | --- |
| Tiles appear, at the right scale, centred on the planet | Yes |
| Longitude/latitude alignment | The Gulf of California and Baja coast sit where the orbital map puts them; no mirrored or rotated globe. Not measured to the kilometre. |
| Hand-over 1.10 R to 1.035 R | Dithered dissolve works (`25-mid-descent`) |
| 30,000 ft floor | Reached (distance 99.993); Grand Canyon relief is real 3D (`27-terrain-30000ft`) |
| Tile brightness | Close to the unlit globe with the sun at 4.0 and a sky light at 1.0 |
| Destruction decal | Paints tiles: ejecta, fire and magma glow show at the mantle crater (`28-terrain-crater`) |
| Night side | City lights restored on tiles through the decal (`24-low-orbit-night-cities`) |

What it took beyond the first build:

- Readiness is "at least 12 tile components exist", not a load percentage. Streaming never
  reaches a high percentage while a 4K view is refining.
- Occlusion culling and fog culling are off on the tileset. The tiles start hidden under
  the opaque globe, and from low orbit the horizon is thousands of kilometres away.
- `ForbidHoles` is on, so a coarse parent shows until its children arrive instead of a hole
  into space. Concurrent loads are raised to 96 tiles and 64 imagery tiles.
- A sky light (engine daylight cubemap) fades in with the hand-over and with daylight under
  the camera. Without it the lit tiles had no fill and the sea rendered black.
- A decal's emissive is scaled by its opacity, so the decal lays a dark base under the
  night side and under burning ground before adding city lights and fire.
- Stars and the Milky Way dim when the camera is deep in a daylit sky.
- Capture holds each low view for 5 to 12 seconds so tiles can refine. A first visit to a
  place is blurry for a few seconds; `cesium-request-cache.sqlite` makes revisits fast.

Still wrong or unfinished:

- Open ocean on tiles is a flat grey-blue that mirrors the cubemap's clouds
  (`23-low-orbit-ocean`). It loses the sunglint and bathymetry colour of the orbital ocean.
- At the mantle crater the decal covers the imagery with one flat ejecta colour. The
  layers need to tint the terrain, not replace it, and crater relief is still colour only.
- Tiles get no atmospheric haze of their own, so distant terrain is too crisp.
- Decal alignment against craters seen from orbit has not been checked side by side.
- Aiming and firing from below 1.035 R has not been played by hand.
- Cesium logs a warning about "Enable World Bounds Checks".
