#include "dialog/GuideHandlers.h"

#include "AppStateGlobal.h"
#include "dialog/Handlers.h"
#include "ui/ClassPickerScreen.h"

#include <cstdio>

namespace selva::dialog
{

void registerGuideHandlers()
{
    // Path commit handlers fire on the LEAVE choice of each terminal
    // topic (signing_accepted / signing_refused). They do NOT fire on
    // the accept/refuse selection itself, because the player can
    // reconsider before leaving. The flag set + class-picker modal
    // launch IS the commitment.

    registerActionHandler("guide_commit_signing",
                          [](const std::string& npc_id, const std::string& topic_id)
                          {
                              setFlag("signing_accepted");
                              setFlag("signing_committed");
                              // Acceptance branches into a four-option modal: Penitent /
                              // Heretic / Wretched / Refuse. The modal commits the
                              // specific class to the profile + sets the path flag
                              // (path_class_picker or path_unburdened) on confirmation.
                              // Per setting.md *The Signing and the commit-fire*.
                              selva::ui::openClassPicker();
                              std::fprintf(
                                  stderr,
                                  "[guide-handler] guide_commit_signing -> opening class-picker "
                                  "modal (npc=%s topic=%s)\n",
                                  npc_id.c_str(), topic_id.c_str());
                              std::fflush(stderr);
                          });

    registerActionHandler(
        "guide_commit_refusal",
        [](const std::string& npc_id, const std::string& topic_id)
        {
            setFlag("signing_refused");
            setFlag("signing_committed");
            setFlag("path_unburdened");
            // Direct commit to Unburdened -- no modal needed; the
            // refusal dialog path is itself the choice. The
            // class-picker modal exists for the accept-branch where
            // class identity has to be picked from three.
            if (auto* profile = activePlayerProfile())
                profile->player_class = PlayerClass::Unburdened;
            std::fprintf(stderr, "[guide-handler] guide_commit_refusal (npc=%s topic=%s)\n",
                         npc_id.c_str(), topic_id.c_str());
            std::fflush(stderr);
        });
}

} // namespace selva::dialog
