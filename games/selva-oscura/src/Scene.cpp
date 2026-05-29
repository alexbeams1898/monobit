#include "Scene.h"

#include <cassert>

namespace selva::scene
{

namespace
{
bool sActive = false;
InputLock sLocks;
const InputLock kZeroLocks{};
} // namespace

void begin(InputLock locks)
{
    assert(!sActive && "selva::scene::begin called while another Scene active");
    sActive = true;
    sLocks = locks;
}

void end()
{
    sActive = false;
    sLocks = InputLock{};
}

bool active()
{
    return sActive;
}

const InputLock& currentLocks()
{
    return sActive ? sLocks : kZeroLocks;
}

void tick(float /*dt*/)
{
    // v2: advance camera scripting, check end-conditions, etc.
}

} // namespace selva::scene
