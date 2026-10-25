# Third-party components and their notices

Deep Snow (on Nexus: Deep Snow Makes You Slow) as a whole is GPL-3.0-or-later (`LICENSE`, `NOTICE.md`). These
components and data are included under their own GPL-compatible licences; their notices are reproduced as those
licences require.

## Ice is Slick 1.0.3.2 - the ICE row's default lists (`include/IceIsSlickTable.h`, `[Surfaces]` in the INI)

Ice is Slick by Ckw25, https://www.nexusmods.com/skyrimspecialedition/mods/190881, GPL-3.0-or-later.

The ICE row's defaults are the ice collision materials (MaterialIce, MaterialIceForm), the frozen-water mesh paths and
the ice texture names that Ice is Slick recognises, read from its published 1.0.3.2 source archive ("Ice is Slick
1.0.3.2 - Source": `src/core/SurfaceClassifier.h`, `src/core/SurfaceCoatingModel.h`, `src/core/CompatibilityRules.h`)
on 2026-10-24, so that the two mods never act on the same ground. They are used as data; no Ice is Slick code is
copied, linked or called. Ice is Slick's licence is the GNU General Public License version 3 or (at its option) any
later version - the same licence as this mod, whose full text is `LICENSE`:

    This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
    later version. This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
    even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
    License for more details.

## CommonLibSSE-NG 3.7.0 - the SE 1.5.97 / AE 1.6.1170 build line (the 1.0.0 DLL)

https://github.com/alandtse/CommonLibVR (branch ng, commit e617713b2ae8a927bf925d1ad138cc48ab72e414), statically linked.

MIT License

Copyright (c) 2018 Ryan-rsm-McKenzie

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## CommonLibSSE-NG 7.2.0 - the Skyrim 1.7.x build line (planned; not in the 1.0.0 download)

https://github.com/alandtse/CommonLibSSE-NG (commit 7a60f4de794095d7b0f8928d1b930a52e9a7da83), GPL-3.0-or-later WITH
Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source).

## spdlog

https://github.com/gabime/spdlog, MIT, Copyright (c) 2016 Gabi Melman. The MIT permission notice above applies.

## SKSE Menu Framework consumer header (`include/SKSEMenuFramework.h`) - the settings page

From https://github.com/QTR-Modding/SKSE-Menu-Framework-3 (Thiago099 / QTR-Modding), copied 2026-08-24 while that
repository was LGPL-2.1 (it became GPL-3.0 on 2026-08-29); modified here to prefer the Apocrypha Menu Framework and to
fall back to SKSE Menu Framework. LGPL-2.1 permits its use in this GPL-3.0-or-later work. The header resolves the
framework's exports at run time; no framework binary is linked or shipped. `include/PreciseSlider.h`,
`include/utils/Toggle.h` and `include/utils/Strings.h` are this project's own code over that header.

## DevBench consumer API (`include/DevBench/`, `source/DevBench/`)

MIT License

Copyright (c) 2026 Alan Tse and devbench contributors

This license applies ONLY to the devbench cross-plugin API glue files (DevBenchAPI.h and DevBenchAPI.cpp), so that any
SKSE plugin may vendor them to interoperate with devbench. The devbench plugin itself is licensed GPL-3.0 (see COPYING
and EXCEPTIONS); this MIT grant does not extend to the rest of devbench.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Dynamic Terrain Deformation

Not included. Deep Snow reads one number, `SnowRaiseHeight`, from DTD's own INI when it is installed, for the units its
page shows. No DTD code is used.
