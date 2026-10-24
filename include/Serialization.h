#pragma once

// Deep Snow - the SKSE co-save (unique id 'DPSN'). Three versioned records:
//   'TRLS' v1  the packed-trail squares (Trails.h)
//   'APLD' v1  the actors holding one of our abilities, for clean-up after a load (Effects.h)
//   'SNFL' v1  the snowfall boost (Movement.h)
// An unknown record or version is skipped with a warning, never fatal. Revert (a new game, loading another save)
// clears all three.

namespace serialization
{
	void Install();  // at plugin load, after SKSE::Init
}
