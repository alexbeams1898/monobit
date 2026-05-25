#include "world/AsyncSceneLoader.h"

#include <cstdio>
#include <exception>

namespace engine::world
{

std::future<void> beginAsyncScenePrepare(AsyncCapableScene& target)
{
    // std::async with launch::async forces a real worker thread (not
    // deferred to the polling caller). For our scale this is fine —
    // scenes load in <100ms and we're polling per-frame anyway.
    // Larger scenes (multi-second loads) would want a dedicated
    // long-lived worker thread + queue, but that's overhead we
    // don't need yet.
    return std::async(std::launch::async, [&target] {
        try
        {
            target.prepareAsync();
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "[scene-async] prepareAsync('%s') threw: %s\n",
                         target.sceneId().c_str(), e.what());
        }
        catch (...)
        {
            std::fprintf(stderr, "[scene-async] prepareAsync('%s') threw unknown\n",
                         target.sceneId().c_str());
        }
    });
}

} // namespace engine::world
