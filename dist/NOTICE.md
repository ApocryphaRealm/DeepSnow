# Deep Snow - copyright and licence

Copyright (C) 2026 ApocryphaRealm

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program (`LICENSE`). If not, see
<https://www.gnu.org/licenses/>.

SPDX-License-Identifier: GPL-3.0-or-later

## Why GPL-3.0-or-later

The mod is planned with two build lines. The Skyrim 1.7.x line statically links CommonLibSSE-NG 7.2.0
(https://github.com/alandtse/CommonLibSSE-NG, commit 7a60f4de794095d7b0f8928d1b930a52e9a7da83), GPL-3.0-or-later WITH
its Modding Exception and GPL-3.0 Linking Exception, whose README requires a linking plugin to be GPL-3.0-or-later or
GPL-compatible. The whole work - both build lines and the source - is GPL-3.0-or-later.

Components under other licences, with their notices: `THIRD_PARTY_NOTICES.md`.

## Origins

Written for this project. No code of Dynamic Terrain Deformation (GPL-3.0-only) or Speed of Strolling is used or read
into this mod: the ground classification follows the general idea of reading the landscape texture under the player
and the navmesh's preferred-path flag, written here from the game's own data.

The ICE row's default lists (`include/IceIsSlickTable.h` and `[Surfaces] sIceMaterials / sIceMeshes / sIce` in the INI)
are the ice collision materials, frozen-water mesh paths and ice texture names that **Ice is Slick** by Ckw25
(https://www.nexusmods.com/skyrimspecialedition/mods/190881, GPL-3.0-or-later) recognises, read from its 1.0.3.2 source
archive so that the two mods never act on the same ground. They are listed as data; no Ice is Slick code is used.
Ice is Slick's licence is the same GPL-3.0-or-later as this mod's.
