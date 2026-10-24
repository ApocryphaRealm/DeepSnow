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
- Default road textures are now vanilla road01* and Northern Roads' grounddirt01, grounddirt01_winter and grounddirt01_trail* (its LRoadDirt01 road and road-edge textures, met on the Whiterun stables road in test 78b3). snowcobble01 is dropped: vanilla's LSnowCobble01 draws SnowRocks01.dds, so that stem never matched.
- Roads are always exempt: no snow band and no stamina drain on a road, whatever the cover, packing or snowfall say (the owner, 2026-10-24). A road is the navmesh preferred-path flag, a texture in sRoad, or ground marked with This is a road. The optional road speed bonus is unchanged. Dirt and other non-snow ground stays penalty-free (fOther 0). The page says so in all eleven languages.
- ICE now matches Ice is Slick's own ice list, taken from Ice is Slick 1.0.3.2 (include\IceIsSlickTable.h): checked first by the collision material under the actor (MaterialIce 873356572 and MaterialIceForm 2431524493, new INI list sIceMaterials), then its 35 frozen-water meshes (sIceMeshes, exact paths; mesh paths now matched with forward slashes and no meshes\ prefix), then its ice texture names (sIce). ICE means no band, no stamina drain and no slowdown, so the two mods never act on the same ground. The DevBench state now reports the support material.
- The milestone-1 ground probe stays in the build but is off by default ([Probe] bEnabled=0); test 78b3 read what it was for.
### Removed
- The fRoad depth key: roads no longer have a depth row, they are always exempt.