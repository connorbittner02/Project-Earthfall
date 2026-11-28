# Real cities at 30,000 ft, volumetric clouds, black-body detonations, corridor blackouts

Branch `feature/photoreal-30k-buildings-vfx`, built on `feature/civ-munitions-growth`.
Three milestones, each verified by native 3840x2160 captures. Frame numbers below refer
to `Media/Capture4K/`.

## Controls and switches added

| Input | Action |
| --- | --- |
| K | Cloud Quality: High (6 march steps) or Ultra (8). Also `-EarthfallCloudQuality=Ultra` |
| `-EarthfallCaptureSet=city`, `fx` or `grid` | Run only that part of the capture, for iteration |

## Milestone 1: real ground and real cities

**Textures.** `Scripts/prepare_pbr_textures.py` downloads four CC0 sets from Poly Haven
(no login) at 2K into `Saved/SourceTextures/pbr/` and packs each into two PNGs: albedo,
and normal XY with roughness in blue. `build_earth_assets.py` imports them.

| Poly Haven asset | Used for |
| --- | --- |
| `rubble` | Crater ejecta and rims, flattened districts |
| `dark_rock` | Crater floors, char, burn scars |
| `concrete_wall_008` | Building facades |
| `aerial_asphalt_01` | Building roofs |

The terrain decal lays rubble and rock tri-planar over the globe wherever something has
been destroyed, tiled every 2.4 km (far larger than life, so it resolves from 30,000 ft),
and lights the normal map against the sun itself, since a decal blends colour only.

**Buildings.** A second Cesium tileset, ion asset 96188 (Cesium OSM Buildings), is shown
below 1.05 planet radii on the same georeference as the terrain. `M_OSMBuildings` gives
the untextured boxes concrete and glass facades and asphalt roofs, lit windows at night,
and reads the simulation maps by longitude and latitude:

- grid health (the blue channel of the city-growth map) switches the windows off, floor
  by floor while a grid is failing;
- the impact, crater and fire maps char the walls, light fires in the storeys, and sink
  each 90 m block of the city by its own amount (a world position offset);
- Cesium makes one material per tile, so the streamer hands each new tile the maps once.

**Night.** Lit cities are no longer procedural dots. The decal's night cover lifts over a
lit city, and a dim sodium sky light shows the streamed photograph, so the lit streets
are the real streets. A dead grid gets the cover back.

**Growth.** Hub cities that pass 125% of baseline get cranes and a first tower; Megacity
six towers and four cranes; Arcology fourteen towers around one 2.5 km tall, and seven
cranes with flashing amber beacons. They are instanced boxes and cones standing on the
terrain height sampled from Cesium, with lit storey bands, and they char, go dark and
fall with the buildings around them.

**Near-ground fixes that came with it.** The simulation maps are 20 to 40 km per texel,
so near the ground they are read through a noise warp that turns straight edges into
ragged ones. Wildfire, unrest fires and lava veins, which are glows from orbit, are
drawn as thin fronts and cracks up close. Sandbox markers no longer balloon into sheets.

## Milestone 2: clouds, detonations, ash

**Clouds.** The deck is a volume. The view ray is marched down through a layer of real
thickness in 6 or 8 steps, each pixel starting at a different depth so the slices become
grain instead of terraces. A cloud stands as tall as it is dense. Each sample is lit by
Beer-Lambert attenuation along a lookup toward the sun and through the cloud above it,
with a two-lobe Henyey-Greenstein phase function for the silver lining. The expensive
part of the weather (drift, cyclone twists, warp) is evaluated three times per pixel and
every march sample is taken from that plane, which keeps the cost near the old shader's.

**Detonations.** Kinetic rods, mantle charges and thermonuclear strikes run three stages:

1. 0 to 0.25 s: a double flash, a flash across the ground, and a Wilson condensation
   dome: a thin shell, bright at its limb, that races out, tears and is gone in a second.
2. 0.25 to 4 s: a black-body fireball. Every pixel has a temperature, from the core
   (4000 K cooling to 2200 K, then 1100 K) down through the troughs of a domain-warped
   turbulence, and the Planck curve turns that into colour and brightness. Gas below
   about 1300 K becomes opaque soot, which closes over the fire from the rim inward.
3. From 2 s: a cooler cap lifts off; `AEarthfallPlume` raises the stem, which now leans
   with the wind by the square of altitude, under a two-layer anvil streaming downwind.

The exotic weapons (plasma lance, black hole, cryo impactor) keep their coloured fireball.

**Ash.** A second ping-pong map rides with the ash map: steam and mist, rock dust, and
carbon times loft. Five variables decide a plume:

| Variable | Effect |
| --- | --- |
| Surface struck | Sea: steam and salt mist, 15% of the carbon. Ice: mist, 10%. City: 160% carbon, less dust. Land: carbon and dust |
| Crater energy | Rock dust rises with each decade of megatons |
| Wind shear | Carbon travels at 0.55 to 2.3 times the wind by loft; steam and dust at half |
| Moisture | Over sea and in the wet latitude bands, carbon and dust rain out up to 3.5x faster |
| Thermal buoyancy | Loft rises with yield; lofted carbon lasts up to 3x longer and is out of the rain |

The CPU plumes that cause fallout casualties carry the same loft.

## Milestone 3: night lights and the grid

- City growth is soft-clamped before bloom, `x / (1 + 0.45 x)`, scaled so baseline is
  unchanged: a city at three times its size is about twice as bright.
- Two colour temperatures: dense cores at 2700 K, the dimmer roads and suburbs at 4500 K.
- Blackouts are no longer discs. A strike destroys the grid only near its centre. Every
  quarter second, substations whose cells have failed shed their load onto neighbours:
  about two links in five are trunk corridors that carry 95% of a fault, the rest 18%,
  and a fault loses 30% per hop, so the failure travels as a front with fingers along
  the trunks for four or five cells. Cells that are hit but not tripped brown out and
  flicker. Restoration works inward from the live edge.

## What differs from the brief

- **Sprawl and megatowers are not Niagara or modelled assets.** They are instanced engine
  shapes with a procedural material. The megatowers are stacked boxes with a spire.
- **Building collapse is a vertex offset**, not debris: blocks sink and lean. A tower at
  the edge of a blast is sheared.
- **Windows are procedural.** OSM Buildings carry no textures; storeys and bays are drawn
  from world position. From 30,000 ft a window is under a pixel, so what shows is
  four-storey panels.
- **Buildings cast no shadows** and the megatowers do not either: the streamed imagery
  has its own baked shadows and a planet-scale shadow pass was not attempted.
- **"Urban = toxic soot" is denser, blacker carbon**, not a separate species.
- **Stage 3 reuses the existing plume actor** with shear and an anvil added; its stem and
  cap are still overlapping soft volumes.
- **Cloud Quality has two levels, 6 and 8 steps.** There is no lower setting.
- **Power lines are a hash of cell pairs**, not real transmission data.

## Verified, and not

The full suite is 115 shots at native 3840x2160: the 71 earlier shots, six city shots
(`69` to `74`), thirty-one effects shots (`75` to `80`, including a twenty-frame burst
through the first second of a detonation at one tenth speed) and seven grid shots (`81`
to `87`). Four consecutive full runs finished with no shader compile errors, the audio
self-test passing, and the simulation reset clean.

**Frame rate: 60 fps is met on average, not in every section.** With another
application holding 30 to 40% of the GPU, four full runs averaged 44.4 to 47.8 fps. With
the GPU free, four more full runs logged 60.4, 58.9, 84.3 and 70.7 fps. That logged
average is slightly flattering, because the slow-motion burst counts ten real seconds as
one, so the capture now logs a frame count and wall-clock time at every shot. By that
measure the last two runs were 80.5 and 66.8 fps over the whole script.

Per section, wall clock, for those two runs:

| Section | Run 3 | Run 4 |
| --- | --- | --- |
| Low orbit and streamed terrain (`22` to `27`) | 66.1 | 75.5 |
| Original orbital strikes with HUD (`01` to `21`) | 49.2 | 57.1 |
| Terrain destruction (`28` to `34`) | 47.2 | 71.5 |
| Sandbox and diplomacy (`35` to `52`) | 49.3 | 59.4 |
| Munitions and civilization (`55` to `68`) | 46.6 | 52.6 |
| Cities at 30,000 ft (`69` to `74`) | 98.6 | 82.9 |
| Clouds (`75` to `77b`) | 126.5 | 55.9 |
| Detonation and plumes (`78d` to `80`) | 129.3 | 63.3 |
| Night lights and grid (`81` to `87`) | 130.5 | 61.6 |

The two runs disagree by a factor of two on some sections with nothing changed between
them, so something outside the game was still taking GPU time intermittently; the cause
was not found. What the table does show: the new city, effects and grid work can run
well above 60, and the older HUD-on sections are the ones that sit in the 50s. The
branch this builds on logged 69 fps for those sections. A test with the cloud march
forced to one step instead of six changed the frame rate by about 6%, so the march is
not what costs them.

Seen in frames:

- `69`: Tokyo at night with OSM buildings, lit windows and the real street pattern. Dim.
- `70`: megatowers and cranes after Tokyo reaches Arcology. `71`: the same view after an
  EMP, dark.
- `72`: Manhattan by day. The buildings are there and read as a skyline; there are no
  building shadows.
- `73`, `74`: after a strike. Scanned rubble and rock on the ground and lava cracks. The
  buildings in frame are charred and standing; a visibly collapsed district was not
  captured, because any strike close enough to collapse buildings also buries the camera
  in its fireball and ash for longer than the shot waits.
- `75` to `77b`: the cloud volume from orbit, at the terminator and skimming the deck.
  Coverage is thinner than the old painted deck and Ultra is hard to tell from High.
- `78a` to `78e`: first flash, Wilson dome, second flash, white-yellow fireball, orange
  fireball, soot jacket. `78f` to `78h`: the mushroom at the limb; small in frame.
- `79`, `80`: sea, city and forest strikes side by side: white steam against brown-grey
  carbon and dust, then both drawn out by the wind. City and forest look much alike.
- `81`, `82`: Japan before and after tripling. Detail survives inside the glow, but the
  grown cores are still close to white.
- `83` to `87`: a blackout spreading as an irregular front and beginning to restore.

Not verified, or wrong:

- Nothing was played by hand; every frame is from the scripted capture.
- `28` (crater at 30,000 ft) is almost entirely dark: the camera is three degrees from
  ground zero and looks straight into the ash column. That has been true since the
  column was added; the committed frame before this branch predated it.
- Brownout flicker is in the shaders but is too fine to judge from a still.
- Rain-out and buoyancy are in the transport shader and the log shows the stamped loft
  and composition per strike; their effect over minutes was not isolated in a frame.
- The megatower collapse and the OSM building collapse offset were not captured.
