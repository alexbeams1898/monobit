#include "dialog/GuideHandlers.h"

#include "AppStateGlobal.h"
#include "dialog/Handlers.h"
#include "ui/ClassPickerScreen.h"
#include "ui/NamePromptScreen.h"

#include <cstdio>

namespace selva::dialog
{

void registerGuideHandlers()
{
    // Open the Beat-3 name-prompt modal (per the locked design: the
    // Guide elicits the name in-dialog, not on a setup screen). Modal
    // writes the typed name into selva::pendingProfile() + sets the
    // "name_given" flag so the next dialog topic gates correctly.
    registerActionHandler("guide_open_name_prompt",
                          [](const std::string& npc_id, const std::string& topic_id)
                          {
                              selva::ui::openNamePrompt();
                              std::fprintf(stderr,
                                           "[guide-handler] guide_open_name_prompt -> modal opened "
                                           "(npc=%s topic=%s)\n",
                                           npc_id.c_str(), topic_id.c_str());
                              std::fflush(stderr);
                          });
    // Open the Beat-4 class-picker modal. Same pattern; modal commits
    // the cosmological identity + (for unnamed runs) calls
    // SaveManager::finalizeUnnamedCharacter to persist the character
    // for the first time.
    registerActionHandler("guide_open_picker",
                          [](const std::string& npc_id, const std::string& topic_id)
                          {
                              selva::ui::openClassPicker();
                              std::fprintf(stderr,
                                           "[guide-handler] guide_open_picker -> picker opened "
                                           "(npc=%s topic=%s)\n",
                                           npc_id.c_str(), topic_id.c_str());
                              std::fflush(stderr);
                          });
}

} // namespace selva::dialog
