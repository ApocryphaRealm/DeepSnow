# Deep Snow Makes You Slow

An SKSE plugin for Skyrim SE 1.5.97 and AE 1.6.1170, with a small light plugin (`DeepSnow.esp`). Deep snow slows you
down and costs a little stamina while you move; packed trails and roads stay easy going. The mod's internal name, DLL,
log and INI are `DeepSnow`.

## What it does

- **Snow Deformation support.** When Community Shaders' Snow Deformation feature is installed and on, Deep Snow reads
  the real snow depth under each actor from its public API (game units, already deepened by its accumulation) and
  turns it into the same depth bands: `fSDFullDepthUnits` units (default 30) count as a full blanket. Roads, ice, roofs
  and packed trails work as before. `[Depth] uSource` picks the source: 0 Auto, 1 Deep Snow's own model, 2 Snow
  Deformation.
- **Its own snow-depth model** otherwise. The snow Dynamic Terrain Deformation draws cannot be read, so Deep Snow
  estimates the snow where an actor stands: the landscape texture underfoot (snow, trail, road, ice or other), the snow cover of the
  terrain blend under the feet, the packing of trails, optional snowfall, and no snow under a roof. 100 % is a full,
  untouched blanket.
- **Five depth bands.** Each has a starting depth, a movement speed and a stamina cost per second. The speed change is a
  SpeedMult ability from `DeepSnow.esp`, added while the actor stands in the band and removed when it leaves - never an
  absolute SpeedMult write - so it adds to whatever other mods do to movement speed.
- **Packed trails.** Every footstep - the player's, an NPC's or a creature's - packs the snow of a 128-unit square, and
  packed snow fills back in over game hours. Trails are kept in the SKSE co-save.
- **Roads are exempt.** The navmesh's preferred-path flag, a road texture, or ground marked as road: no band and no
  stamina cost there.
- **Ice is left to Ice is Slick.** The ice row uses Ice is Slick's own ice list (collision materials, frozen-water meshes
  and ice textures), so the two mods never act on the same ground.
- **A settings page** in the Apocrypha Menu Framework or SKSE Menu Framework: status under your feet, the depth-band
  table with precise sliders, switches, trail and snowfall settings, buttons that mark the ground under you as snow,
  trail, road or ice, and Save / Reload / Restore defaults. Eleven languages.
- **DevBench tools** `deepsnow.state` and `deepsnow.control` for testing.

No game code is hooked. The plugin reads game data (landscape textures, navmesh flags, collision materials, a ray for
roofs) and applies its effects through spells from its own plugin.

## Building

Visual Studio 2022 (C++ desktop workload), CMake and a vcpkg checkout in `VCPKG_ROOT`.

    configure.bat
    build.bat

`find-msvc.bat` locates Visual Studio. The DLL and PDB land in `build\relwithdebinfo\`; `dist\` holds everything else
that ships. `py -3 tools\build-esp.py` writes `dist\DeepSnow.esp`; `py -3 tools\make-translations.py` writes the eleven
translation files. `configure17.bat` / `build17.bat` are the planned Skyrim 1.7.x line (CommonLibSSE-NG 7.2), not part
of 1.0.0.

## Licence

GPL-3.0-or-later (`LICENSE`, `NOTICE.md`). Components under other licences, and the Ice is Slick data the ice row uses,
with their notices: `THIRD_PARTY_NOTICES.md`.

## Credits

- Ckw25 for [Ice is Slick](https://www.nexusmods.com/skyrimspecialedition/mods/190881) (GPL-3.0-or-later): the ice
  materials, meshes and textures in the ice row are its list, read from its 1.0.3.2 source.
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng) and its contributors (originally
  CommonLibSSE by Ryan McKenzie).
- QTR-Modding / Thiago099 for the [SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3) consumer
  header the settings page draws through.
- DevBench for the testing API.
- Dynamic Terrain Deformation: Deep Snow only reads its `SnowRaiseHeight` for the units shown. None of its code is used.
- josefplyr1 for Snow Deformation (Community Shaders) and its public API, whose header (`include/SnowDeformationAPI.h`,
  GPL-3.0-or-later) Deep Snow uses to read the snow depth.
