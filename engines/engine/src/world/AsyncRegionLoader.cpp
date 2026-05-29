#include "world/AsyncRegionLoader.h"

#include <cstdio>
#include <exception>

namespace engine::world
{

std::future<void> beginAsyncRegionPrepare(AsyncCapableRegion& target)
{
    // std::async with launch::async forces a real worker thread (not
    // deferred to the polling caller). For our scale this is fine —
    // regions load in <100ms and we're polling per-frame anyway.
    // Larger regions (multi-second loads) would want a dedicated
    // long-lived worker thread + queue, but that's overhead we
    // don't need yet.
    return std::async(std::launch::async,
                      [&target]
                      {
                          try
                          {
                              target.prepareAsync();
                          }
                          catch (const std::exception& e)
                          {
                              std::fprintf(stderr, "[region-async] prepareAsync('%s') threw: %s\n",
                                           target.regionId().c_str(), e.what());
                          }
                          catch (...)
                          {
                              std::fprintf(stderr,
                                           "[region-async] prepareAsync('%s') threw unknown\n",
                                           target.regionId().c_str());
                          }
                      });
}

} // namespace engine::world
