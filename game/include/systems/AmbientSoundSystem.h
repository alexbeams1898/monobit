#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// AmbientSoundSystem -- plays random sounds from an entity's AmbientSound
// component pool at randomized intervals.  Extensible via AmbientSound::Mode.
// ---------------------------------------------------------------------------

class AmbientSoundSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
