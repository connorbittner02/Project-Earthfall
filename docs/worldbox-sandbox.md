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

## Follow-up pass

Verified by the same 4K capture, now 56 shots at about 73 fps.

**Naval fleets and air wings.** A strike group is now one of two things. A *naval fleet*
routes over a 1.5 degree water grid (Dijkstra over every cell its sea connects to) to the
reachable water nearest the enemy capital, never crossing land, and shells the first
coast on the line to the capital with real missiles. An *air strike wing* flies the great
circle above the clouds. The AI sends a fleet when the capital is within 0.40 rad of
reachable water and a wing otherwise; the Strike Group tool makes a fleet at sea and a
wing over land. Capture: `50-naval-fleet-under-way`, `51-naval-fleet-bombards-coast`.

**Force Peace.** Ends every war the nation is in, drops pending treaty joins, removes
units on both sides and destroys missiles still in flight. Capture `52-force-peace`: a
France-USA-UK war on Brazil went from 3 wars to 0, with 0 missiles in flight five seconds
later.

**Bio on streamed terrain.** The terrain overlay samples the bio mask: plague is a green
ground haze, rage is small flickering fires under smoke. Combat sparks are hidden below
1.02 R, where they would hang in the sky. Capture: `53-terrain-plague-rage`,
`54-terrain-rage-low`.

**Refugee crises.** Every four seconds, a nation with five million displaced and a fifth
of its people on the move presses a quarter of them on each neighbour. That strain is
added to the neighbour's own stability stress (capped at 0.35), so it can reach emergency
or martial law with no strike on it. Above 0.18, a neighbour may declare war on a source
whose government still stands. In the capture this started Panama-Colombia and
Trinidad-Venezuela wars unprompted.

**Synth self-test.** `UEarthfallAudioComponent::RunSelfTest()` renders every voice offline
at 48 kHz at capture start and logs `EARTHFALL_AUDIOCHECK`. Output is `0.95 * tanh(1.1x)`,
so no sample can exceed 0.95. Result of the last run:

| Voice | Peak | RMS | First 1 ms | Last 50 ms |
| --- | --- | --- | --- | --- |
| Ambience, orbit | 0.216 | 0.071 | 0.0002 | n/a |
| Ambience, low with firestorm | 0.410 | 0.059 | 0.0002 | n/a |
| Launch | 0.208 | 0.042 | 0.0000 | 0.0000 |
| Boom | 0.707 | 0.282 | 0.0153 | 0.0000 |
| Alert | 0.203 | 0.056 | 0.0116 | 0.0000 |
| Everything at once | 0.950 | 0.356 | n/a | n/a |

Boom sits 14.0 dB over the alert. The test caught one fault: the alert's static started
too abruptly (0.023 in the first millisecond); its attack is now 10 ms. This checks
levels and edges. It does not tell anyone whether the sounds are pleasant, and the
everything-at-once case is riding the limiter.

### Still unverified or weak

- **Secondary wildfires are confirmed by the log, not cleanly by a frame.** Each run logs
  50 to 80 `EARTHFALL_SECONDARY_FIRE` ignitions. `34b-secondary-wildfire` looks across one
  such site (the Colombian Amazon, 5,450 km from the mantle strike) and shows flames, but
  that region is also about 1,200 km from the earlier Amazon rod strike, whose own fire
  front spreads that way. The frame cannot tell the two apart.
- Air strike wings were not captured on screen.
- Fleet routes follow grid cells and zigzag; narrow straits under 1.5 degrees are closed.
- Refugee wars can be frequent among small neighbours after a large strike.
- Rage fires on terrain are small points; they are easy to miss from 40 km.
