#pragma once

namespace selva::dialog
{

// Register dialog action / condition handlers referenced by
// config/npcs/guide.json. Call once at boot BEFORE the dialog
// topic registry loads so handler-key references resolve.
void registerGuideHandlers();

} // namespace selva::dialog
