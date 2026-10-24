# DeepSnow - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Each version carries its
**version-ledger status**, so this file cannot quietly claim more than the ledger does:

* **working** - observed running in game
* **untested** - built and packaged, not yet confirmed
* **failed** - built but crashed or malfunctioned; the number was reclaimed
* **scratch** - a hypothesis-test build that never held a real number

## 1.0.0 - 2026-10-24 - scratch

### Added
- Spike (milestone 1): a read-only ground probe. Twice a second it logs, for the player, the landscape texture underfoot (the engine's TES::GetLandTexture pick and the LAND quadrant's base texture and blend layers at the nearest vertex), the navmesh triangle underfoot and its preferred-path flag, a havok ray straight up (roof or overhang) and one straight down, and position, worldspace, cell, interior, mounted and swimming. DevBench tool deepsnow.state (op=state, op=history). Nothing in the game is changed.
