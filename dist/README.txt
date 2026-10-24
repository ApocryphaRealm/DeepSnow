Deep Snow
Version 1.0.0

SKSE plugin with a small light plugin (DeepSnow.esp). Deep snow slows you down and costs a little stamina; packed
trails and roads stay easy going.

HOW IT WORKS
  - Deep Snow keeps its own model of how deep the snow is where someone stands (no mod can read the real snow that
    Dynamic Terrain Deformation draws): 100 % is a full, untouched blanket. Snow textures count as full snow, snowy path
    textures as a trench, roads, rocks and bridges as no snow, and there is no snow under a roof. Every footstep -
    yours, an NPC's or a creature's - packs the snow down, and packed snow fills back in over a game day.
  - Five depth bands, each with a depth, a speed and a small stamina cost per second while you move. The speed change
    is a magic effect (an ability from DeepSnow.esp) added while you stand in the band and removed when you leave, so it
    adds to whatever Inertia, Dynamic Sprint, Survival Mode or any other mod does to your speed.
  - Ice is left to Ice is Slick: no slowdown and no stamina cost on ice.
  - Optional: faster travel on roads, snowfall that deepens the snow, NPCs slowed too (all off by default).

SETTINGS
  The Deep Snow page in the Apocrypha Menu Framework (or SKSE Menu Framework) - changes apply at once, Save keeps them.
  The same settings are in SKSE\Plugins\DeepSnow.ini. If another mod's snow or path texture is not recognised, stand
  on it and press "This is snow" (or trail, road, ice) on the page.

REQUIREMENTS
  SKSE64, Address Library for SKSE Plugins. Optional: Apocrypha Menu Framework for the page, Dynamic Terrain
  Deformation (its snow height is read for the units shown), DevBench (tools deepsnow.state and deepsnow.control).
  Enable DeepSnow.esp in the load order.

Runs on Skyrim SE 1.5.97 and AE 1.6.1170 (CommonLibSSE-NG 3.7). The .pdb ships beside the DLL for Crash Logger.

LICENCE

GPL-3.0-or-later: LICENSE and NOTICE.md. Components under other licences, with their notices: THIRD_PARTY_NOTICES.md.
