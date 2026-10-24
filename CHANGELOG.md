# DeepSnow - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Each version carries its
**version-ledger status**, so this file cannot quietly claim more than the ledger does:

* **working** - observed running in game
* **untested** - built and packaged, not yet confirmed
* **failed** - built but crashed or malfunctioned; the number was reclaimed
* **scratch** - a hypothesis-test build that never held a real number

## 1.0.0 - 2026-10-24 - untested

### Added
- Settings page on the Apocrypha Menu Framework: status under your feet, the depth-band table with precise sliders, switches, trail and snowfall settings, buttons that mark the ground under you as snow, trail, road or ice and save it, Save / Reload / Restore defaults; every string in eleven languages.
- DevBench tool deepsnow.control (forceband, mark, set, save, reload, restore, cleartrails, stamp) beside the extended deepsnow.state (tick, effects, settings, trails, probe).
- Optional faster travel on roads (off by default), optional snowfall deepening (off by default), NPCs and creatures optionally slowed too (off by default).
- Packed trails from every actor: a pass through a 128-unit square packs it, packing fills back in over game hours (faster while it snows), and the squares are kept in the SKSE co-save (record TRLS v1; APLD v1 remembers who holds an ability, SNFL v1 the snowfall boost).
- The surface table (SNOW, TRAIL, ROAD, ICE, plus objects and other ground) read from texture lists in the INI; ICE (frozenmarshice01/02 and ice meshes) is left to Ice is Slick - no slowdown and no stamina cost there.
- The depth model: Deep Snow's own estimate of the snow where an actor stands (the surface's depth x snow cover under the feet x trail packing x snowfall, 0 under a roof), measured in % of a full blanket (DTD's SnowRaiseHeight, read from its INI, for the units shown).
- Deep snow slows you and costs a little stamina: five configurable depth bands (a depth threshold, a speed and a stamina cost per second each), applied as SpeedMult ability effects from the new DeepSnow.esp (light plugin) that are added while you stand in a band and removed when you leave - never an absolute SpeedMult write, so they stack with Inertia, Dynamic Sprint and Survival Mode.
- Spike (milestone 1): a read-only ground probe. Twice a second it logs, for the player, the landscape texture underfoot (the engine's TES::GetLandTexture pick and the LAND quadrant's base texture and blend layers at the nearest vertex), the navmesh triangle underfoot and its preferred-path flag, a havok ray straight up (roof or overhang) and one straight down, and position, worldspace, cell, interior, mounted and swimming. DevBench tool deepsnow.state (op=state, op=history). Nothing in the game is changed.

### Changed
- The milestone-1 ground probe stays in the build but is off by default ([Probe] bEnabled=0); test 78b3 read what it was for.