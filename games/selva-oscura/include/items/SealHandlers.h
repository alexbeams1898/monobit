#pragma once

namespace selva::items
{

// Register the Seal's use-action ("consume_seal") + use-condition
// ("unjudged_only") handlers. Call once at boot before item registry
// loads so JSON-referenced handler keys exist.
void registerSealHandlers();

} // namespace selva::items
