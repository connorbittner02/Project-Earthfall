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
