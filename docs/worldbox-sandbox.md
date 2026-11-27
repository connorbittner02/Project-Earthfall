# WorldBox-style sandbox

Branch `feature/worldbox-earth-sandbox`, built on the movie-grade destruction branch.
Verified by a native 3840x2160 capture: 50 shots, about 72 fps average on an RTX 4080.

## Controls

| Input | Action |
| --- | --- |
| Tab, or click a tab | Cycle the command dock: ORBITAL, NATURE, DIPLOMACY |
| 1-6 | Select a tool on the open tab |
| LMB | Use the selected tool at the cursor |
| M or MMB | Country inspector |
| H | HUD: full, ticker only, hidden |
| R | Reset the world, including wars, plagues, satellites and debris |

## The dock

| ORBITAL & NUCLEAR | BIO & NATURE | DIPLOMACY & SPAWNERS |
| --- | --- | --- |
| Kinetic Rod | Plague Rain | Whisper of War (click A, then B) |
| Plasma Lance | Rage Outbreak | Force Peace |
| Mantle Charge | Cure Rain | Defense Satellite |
| Micro Black Hole | Super-Volcano (land) | Strike Group |
| Cryo Impactor | Tsunami Trigger (sea) | Rebel Insurgency |
| Thermonuclear | Meteor Shower | |

## Systems

**Factions** (`UEarthfallSandboxSubsystem`). Fourteen major powers start with a capital,
a bloc, a military strength and a warhead stockpile. Any other nation becomes a faction
when it is drawn into a war. Power grid, stability and defense readiness are read live
from the population simulation. The Country Dossier shows military, warheads, wars,
allies and infected.

**Wars.** Whisper of War starts one; allies in the same bloc join by treaty a few
seconds apart; a nation struck from orbit may blame a rival. Warring factions fire real
`AEarthfallProjectile` missiles on ground-to-ground arcs at enemy capitals, escalate to
quarter-yield warheads after a few exchanges, and send strike groups that bombard the
coast. A nation with no power launches nothing, and a collapsed one stops fighting.

**Defense.** A struck nation launches an interceptor with probability
`readiness * survivors * grid^2 * 0.85`, one shot per 1.6 s reload, so a salvo gets
through. Defense satellites cover the ground beneath them. Only rods, bolides and
warheads can be hit.

**Kessler debris.** Each launch and each kill adds to a debris level that decays slowly.
The ring fills in visibly, and above 0.75 it destroys munitions on its own.

**Bio.** Plague Rain drops a zone that drifts on the wind, grows, kills and spreads: along
open air routes to hub cities, or over the nearest border, up to three generations. City
lights under it turn green, then go out. Rage and insurgency zones kill, displace and
wreck the grid, with combat sparks; rage spills over borders, an insurgency ends when the
government falls. Cure Rain clears every zone within 0.22 rad, restores power and rains
out fallout plumes there.

**Traffic.** 64 air routes between 20 hub cities and 12 sea lanes of four ships each.
Flights are grounded when either end is at war, under martial law or below 55% grid;
ships stop while a tsunami is running.

**Nuclear winter.** Below 6 C or 40% sunlight a frost and sea-ice front advances from the
poles, with a famine toll while it holds. It retreats five times slower than it advances.

**Audio** (`UEarthfallAudioComponent`). Orbital drone, wind and firestorm crackle by
altitude; launch whoosh; delayed sub-bass boom; static and an emergency tone on war or
nuclear headlines.

## What is not what was asked

- **Audio is not a MetaSound graph.** The MetaSound plugin is enabled and its modules are
  linked (the build proves that), but MetaSound sources are assets that need the editor
  GUI. The sounds are a hand-written synth, and **nobody has listened to them**: captures
  run with audio off.
- **Markers are GPU-instanced meshes**, like the ejecta, not Niagara.
- **Strike groups** are one unit type that sails a great circle, crossing land if it has
  to. There are no separate naval and air units.
- **Defense satellites** intercept for whoever is below, not for an owner.
- **Plague and rage do not show on streamed Cesium terrain**: the terrain decal does not
  sample the bio mask. Frost does.
- **Infected counts are estimates** from the population under each zone.
- **The nuclear-winter shot pins the ice level.** The scripted strikes do not cool the
  planet to 6 C on their own.
- **Autonomous wars** come from blame after a strike and from treaties. Refugee waves do
  not start wars by themselves.

## Not verified

- Nothing was played by hand. Clicking tabs, the two-click Whisper of War, and every tool
  were driven from the capture script through the same functions the clicks call.
- Strike groups were not captured on screen, and Force Peace was not exercised.
- The `41-interceptors` frame shows one cyan interceptor near the salvo; interceptions are
  otherwise confirmed from the log (8 in the final run).
