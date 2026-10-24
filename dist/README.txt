Deep Snow
Version 1.0.0 (spike build, scratch - not for release)

SKSE plugin, milestone 1 of Deep Snow (deep snow slows you and drains a little stamina; packed trails and roads stay
fast). This build is ONLY the spike: a read-only ground probe. It changes nothing in the game - no movement change, no
stamina drain, no effect, no hook.

Twice a second it writes one line to Documents\My Games\Skyrim Special Edition\SKSE\DeepSnow.log with:
  - position, worldspace, cell (interior or exterior grid), location, mounted, swimming, in water, SpeedMult;
  - the landscape texture under the player: the game's own pick (the one footsteps use) and the land quadrant's base
    texture and blend layers at the nearest vertex (texture form, plugin, diffuse path, material);
  - the navmesh triangle under the player and its flags, including the "preferred path" flag roads carry;
  - a ray straight up (is the player under a roof or overhang: distance, what it hit) and one straight down.

DevBench tool "deepsnow.state": op=state (the newest sample) or op=history n=N.

Runs on Skyrim SE 1.5.97 and AE 1.6.1170 (CommonLibSSE-NG 3.7). Settings: SKSE\Plugins\DeepSnow.ini.
The .pdb ships beside the DLL for Crash Logger.

LICENCE

GPL-3.0-or-later: LICENSE and NOTICE.md. Components under other licences, with their notices: THIRD_PARTY_NOTICES.md.
