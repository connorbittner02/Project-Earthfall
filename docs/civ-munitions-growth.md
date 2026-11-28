# Munitions, growing cities and civilization orders

Branch `feature/civ-munitions-growth`, built on the sandbox branch. Verified by a native
3840x2160 capture: 68 shots, about 70 fps average on an RTX 4080.

## Controls added

| Input | Action |
| --- | --- |
| F, or the SHELL strip under the multipliers | Cycle the loaded munition |
| Tab | The dock now has a fourth tab, CIV |

## Munitions

Chosen with F for player launches, and by tech tier for faction missiles and units. Beams
and the singularity take no shell.

| Shell | Effect |
| --- | --- |
| Standard HE | The weapon as it was |
| MIRV Cluster | The bus splits at 55% of its flight into six sub-munitions at 0.45x radius and 0.30x yield: one centre, five on a ring. Each is a separate target for point defense. |
| Incendiary | No crater or deformation, a 2.4x flash, a scorch core 0.6x wide and a filled fire footprint 1.9x wide |
| EMP Grid-Buster | No crater, scorch, ash, plume or deaths. Grids fail out to 3.5x the blast radius and ground batteries there are blind for 25 s |
| Bunker-Buster | 0.4x radius, 3x crater depth, cannot be intercepted. On a faction's soil: military x0.72, 40% of its warheads gone, batteries down 20 s |

Factions: Conventional fires Standard only; Hypersonic adds Cluster, Penetrator and
Incendiary; Orbital Defense and above add EMP.

## Living cities

- Every population cell keeps its baseline. In a nation that is Stable or in Emergency,
  with power above 70% and few displaced, cells grow 0.3% a second up to 3x baseline.
  Grids repair 0.6% a second where people remain, and the displaced drift home.
- `CityGrowthTexture` (512x256, one pixel per cell) carries settled population over
  baseline in R and the nation's tech tier in G. In `earth_surface.hlsl` city lights dim
  below baseline, brighten above it, and spread through eight taps on the night map
  reaching up to seven texels (about 35 km).
- Hub cities are tracked by everyone within 220 km: 1.5x baseline is MEGACITY, 2.2x is
  ARCOLOGY. Promotions make the news bar; the dossier shows a nation's largest hub.

## The CIV tab

| Tool | Effect |
| --- | --- |
| Drop Resources | Grid fully repaired, growth x10 for 40 s, military +12 |
| Tech Uplift | One tier: Conventional, Hypersonic, Orbital Defense, Fusion Shielded |
| Command Nation | Selects who obeys; highlights it and opens its dossier |
| Order: Attack | War on the clicked nation; an army if there is a land route, a fleet if there is a sea route, an air wing, and a three-missile salvo |
| Order: Mobilize | Six patrols instead of two, military +8, interception +0.20, reload 0.8 s instead of 1.6 s |
| Order: Evacuate | Holds fire, recalls its units, repairs the grid, growth x6 for 60 s |

Each tech tier: units 25% faster, interception +0.12 (another +0.15 at tier 4), whiter and
brighter city lights, and at tier 4 a faint cyan shield dome over the capital.

**Ground armies** march the great circle while there is land under them and halt at a
coast, shelling the capital if it is within 0.30 rad. Every faction shows a two-patrol
garrison at its capital. Hostile units that meet fight; the weaker military usually loses.

## What differs from the brief

- **A fourth tab, not Tab 3.** Six more tools did not fit in the diplomacy tab's six rows.
- **Orders are tools, not HUD buttons.** Command a nation, then pick an order row.
- **Evacuate does not move people inland.** It holds fire, recalls units and rebuilds.
- **Munition select is F and the strip**, not 1-5, which select tools.
- **Markers are instanced meshes**, as before.
- Armies do not path around seas: with no all-land great circle, no army is sent.
- The garrison patrols are visual; they do not fight.

## Verified, and not

Verified in frames: the MIRV split and its bursts (`55`, `56`), the EMP blackout (`57`,
`58`), Japan sprawling brighter with Osaka at ARCOLOGY +200% (`59`, `60`), Brazil at
Fusion Shielded with its shield (`61`), the ordered attack on Argentina (`62`, `63`),
Mobilize and Evacuate in the dossier (`64`, `65`) and the reset (`66`).

Verified by log only: tier promotions, the army and fleet being created, one unit
skirmish, and the state after reset (tier, orders, units, wars, shell).

Not verified:

- Nothing was played by hand; every tool was driven through the functions the clicks call.
- Incendiary and Bunker-Buster have no dedicated frames.
- Balance: natural growth adds about 1% to world population every three seconds.

## Visual polish pass

Verified by a native 3840x2160 capture: 71 shots, 68.9 fps average.

- **Sprawl grid (`59`, `60`).** The growth texture is now sampled through a noise-bent UV
  and a five-tap average, the sprawl is broken by a patch noise, and the eight night-map
  taps are rotated and stretched per place. The 78 km dot grid is gone. A first version
  with eight fixed taps drew streaks; the sprawl is now a jittered twelve-tap disc blur,
  which reads as soft rounded glows. A fully grown Japan still blows out to near-white.
- **Unit silhouettes (`62b`).** Armies and air wings are cones (arrowheads) laid along
  their heading, fleets are long cyan hulls. Tints: armies amber, fleets cyan-blue, air
  wings white-gold. The markers are additive, so the army tint is a deeper, dimmer
  orange than amber; over daylit ground that lands on amber, where a true amber washed
  out to pale gold. `62b` shows amber army wedges beside white air wings; no fleet
  happens to be in that frame (fleet hulls are in `62`).
- **Rage fires (`53`, `54`).** The rage mask is fractional over most of an outbreak, which
  had left the fires as a pale peach wash. Fires and smoke now switch fully on above a
  low threshold: glowing orange clusters several kilometres across with dark smoke
  downwind, and the combat embers carry dark smoke puffs at low altitude. The clusters
  are soft blobs, not flame shapes.
- **Tech tiers at night (`67`, `68`).** Japan uplifted to Fusion Shielded beside
  Conventional China: whiter, brighter lights and the cyan shield over Tokyo.
