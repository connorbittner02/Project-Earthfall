# Movie-grade destruction

Branch `feature/movie-grade-destruction`, built on the Cesium terrain branch. Verified with
a native 3840x2160 capture: 35 shots, about 78 fps average on an RTX 4080.

## 1. Kinetic-energy impacts and tsunamis

`FEarthfallImpactor` (`EarthfallTypes.h`) turns the two existing multipliers into a physical
body. Blast radius scales the diameter; yield scales the kinetic energy, so entry speed
goes with the square root of yield.

| Weapon | Body at 1x | Speed at 1x | Energy at 1x |
| --- | --- | --- | --- |
| Kinetic rod | 20 m tungsten | 11 km/s | 1.2 Mt |
| Mantle charge | 350 m steel | 12 km/s | about 3,000 Mt |
| Cryo impactor | 1.2 km ice | 20 km/s | 39,700 Mt |

`E = 1/2 m v^2` with `m = density * pi/6 * d^3`. Beams, the singularity and the warhead are
not kinetic and carry a yield in megatons instead. The HUD shows the body, its speed and
its energy under the multipliers.

An ocean strike displaces water from a transient cavity of radius
`R = 1/2 (E / (rho g))^(1/4)`, a hemisphere of volume `2/3 pi R^3`. A 1x cryo impactor gives
a 5.7 km cavity and 388 km3. Wave strength is a log scale of that volume (0.25 at the
threshold, 1 near 10,000 km3) and drives three things in `TsunamiRT`:

- the height of the initial mound,
- the wave speed (0.85x to 1.05x, the range the explicit step stays stable in),
- how far the flood runs inland and how hard the shoreline takes it (`Flood` parameter).

Strikes that displace under 0.05 km3 raise no tsunami.

## 2. Splash crown, steam and ash columns

`AEarthfallPlume` is the tall, slow part of an impact.

- Ocean: a two-layer spray crown (a procedural flaring curtain shaded by `M_FXSpray`:
  rising streak noise erodes it and it thins to nothing at the top and outer rim) to about 1.035 R
  and a steam column with a skirt and cap to 1.06 R.
- Land: an ash stem to 1.08 R, a skirt where it punches through the cloud decks, and a
  mushroom cap that keeps spreading. `M_FXVolume` (`fx_volume.hlsl`) shades the meshes as
  billowing volumes: dense through the middle, nothing at the silhouette, noise-eroded.
  A second noise field bites hardest at grazing angles, so the outline breaks into
  billows, and the stem's flat ends fade out so it merges into the cap.

The column's shadow is analytic. Each shock slot carries the column height in `C.w`, and
`ColumnShadow()` in `common.hlsl` darkens the ground and both cloud decks down-sun of it.
The terrain decal applies the same shadow to streamed tiles.

## 3. Sub-orbital ejecta and re-entry

`AEarthfallEjecta` throws up to 2,600 fragments per strike (6,000 alive at once) in a
45-75 degree cone. Each one is integrated under inverse-square gravity toward the planet's
centre, so the curtain climbs to about 1.25 R, hangs, and falls back. Fragments are thin
magma-amber shards with long tails, sized to stay readable from orbit. Falling fragments
below 1.07 R turn into bright streaks. About 1.5% are major fragments: where they land
`SeedSecondaryImpact()` raises a dust plume in `AshPlumeRT` and, on land, lights a fire in
`FireMaskRT`.

**This is not a Niagara GPU simulation.** Niagara systems cannot be authored without the
editor GUI, and every asset in this project is built from scripts. The fragments are
integrated on the CPU and drawn as two GPU-instanced batches. The motion is the same
ballistic arc a GPU emitter would compute; what is missing is per-fragment dust trails.

## 4. Soot twilight and terrain haze

`UpdateEnvironment()` folds `SootTeragrams` into atmosphere health and sunlight. The
limb, the surface in-scatter and the terrain haze all shift from blue to amber-red on a
curve that holds blue until soot builds, then turns quickly.

Streamed tiles get distance haze from the terrain decal: more air in front of far ground,
a scale height that grows with camera altitude, and the same soot-driven colour. It is
done in the decal, not in Cesium's tileset material, which stays stock.

## Capture shots

| Shot | Shows |
| --- | --- |
| `29-splash-crown` | Ocean impact south of Mexico: crown, steam column, spray |
| `30-tsunami-terrain-coast` | The wave flooding the streamed Oaxaca coast |
| `31-ejecta-launch`, `32-ejecta-arc`, `33-reentry-streaks` | Mantle strike near the limb: curtain up, arc, fall |
| `34-ash-column-terrain` | The same strike's ash column over streamed terrain in red haze |

## Second polish pass

- Ocean strikes dig nothing: no crater stamp, no mesh deformation, no scorch and no melt
  veins on open water, so no coarse crater polygon sits under a splash.
- The plume is built from overlapping soft puffs (instanced spheres shaded by
  `M_FXVolumeInstanced`) instead of a cylinder, a disc and a spray curtain. Land: a stack
  of ash puffs, a ring at the cloud tops, a lobed cap and a faint ember glow under it.
  Ocean: a ring of sea-spray puffs, a steam column and a flat churning foam ring.
- Flooded ground on streamed terrain is darkened wet mud, not pale foam; white foam is
  kept to the moving wave crest. Run-up is confined to the coast (0.55-0.80 kept per
  texel, down from 0.84-0.955).
- Flood roughness is 0.55, not the 0.15 first asked for. At 0.15 and 0.28 the wet ground,
  seen at a grazing angle, mirrored the warm horizon and read as bright sand dunes.

## Known limits

- Flooding on streamed terrain is a wide dark wet band, widened from the 39 km flood
  mask to reach the real shore. It does not follow relief: hills inside the band are
  darkened like valleys. Deciding it from the tiles' own height was tried and backed out,
  because coarse distant tiles sag below sea level and flooded whole hill ranges.
- A thin tan band can show at the far horizon of a flooded coast, where damp ground
  still catches the warm sky at a grazing angle.
- The ember glow under the ash cap is too faint to see in the capture.
- The plume, crown and column are shaded meshes, not volumetric simulations. Up close the
  crown is a translucent curtain and the column a noise-eroded cylinder and cap.
- A dark polygonal patch shows under the crown from low orbit: that is the orbital globe's
  crater on its 160x320 mesh.
- The CPU tsunami that times casualties still uses the fixed wave speed, so landfall
  headlines can lead or lag the visible wave by up to 15%.
- Flooded terrain reads pale, closer to frost than to wet ground.
- Stars still show faintly through the sky at around 1 unit of altitude.
- The column shadow is one soft wedge per column; it does not follow the cap's shape.
- Not played by hand: all of this was checked from scripted captures.
