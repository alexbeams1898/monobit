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
    // Guide elicits the name in-dialog, not on a setup screen). If the
    // active profile already has a non-empty name we SKIP the modal and
    // jump straight to the class picker -- this is the reload-mid-
    // Signing path: the player typed a name in a previous run, the
    // autosave persisted it, but the Signing did not commit, so the
    // chain re-runs. Forcing a re-type would be silly. The Signing
    // chain commits atomically at the picker per
    // [[project_atomic_signing_chain]] (gating moved from name_given
    // to signing_committed).
    registerActionHandler("guide_open_name_prompt",
                          [](const std::string& npc_id, const std::string& topic_id)
                          {
                              const auto* profile = selva::activePlayerProfile();
                              const bool has_name = (profile != nullptr) && !profile->name.empty();
                              if (has_name)
                              {
                                  selva::ui::openClassPicker();
                                  std::fprintf(stderr,
                                               "[guide-handler] guide_open_name_prompt -> name "
                                               "already given (%s); skipping to picker "
                                               "(npc=%s topic=%s)\n",
                                               profile->name.c_str(), npc_id.c_str(),
                                               topic_id.c_str());
                              }
                              else
                              {
                                  selva::ui::openNamePrompt();
                                  std::fprintf(stderr,
                                               "[guide-handler] guide_open_name_prompt -> modal "
                                               "opened (npc=%s topic=%s)\n",
                                               npc_id.c_str(), topic_id.c_str());
                              }
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
    // Marks the moment the Guide tells the Vagrant he poisoned the
    // carcass Lupa was feeding from (first_meeting_b). The flag is
    // the trigger for the knows_guide_poisoned_carcass insight node,
    // which combines with knows_lupa_felled to support the
    // concludes_guide_killed_lupa deduction on the Mind sub-page.
    registerActionHandler("guide_confess_carcass",
                          [](const std::string& npc_id, const std::string& topic_id)
                          {
                              selva::setFlag("guide_confessed_carcass");
                              std::fprintf(stderr,
                                           "[guide-handler] guide_confess_carcass -> flag set "
                                           "(npc=%s topic=%s)\n",
                                           npc_id.c_str(), topic_id.c_str());
                              std::fflush(stderr);
                          });
}

} // namespace selva::dialog
