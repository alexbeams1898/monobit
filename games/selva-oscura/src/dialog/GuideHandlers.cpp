#include "dialog/GuideHandlers.h"

#include "AppStateGlobal.h"
#include "dialog/Handlers.h"

#include <cstdio>

namespace selva::dialog
{

void registerGuideHandlers()
{
    // Path commit handlers fire on the LEAVE choice of each terminal
    // topic (signing_accepted / signing_refused). They do NOT fire on
    // the accept/refuse selection itself, because the player can
    // reconsider before leaving. The flag set IS the commitment.

    registerActionHandler(
        "guide_commit_signing",
        [](const std::string& npc_id, const std::string& topic_id)
        {
            setFlag("signing_accepted");
            setFlag("signing_committed");
            setFlag("path_class_picker");
            std::fprintf(stderr, "[guide-handler] guide_commit_signing (npc=%s topic=%s)\n",
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
            std::fprintf(stderr, "[guide-handler] guide_commit_refusal (npc=%s topic=%s)\n",
                         npc_id.c_str(), topic_id.c_str());
            std::fflush(stderr);
        });
}

} // namespace selva::dialog
