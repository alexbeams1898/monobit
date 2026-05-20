#include "combat/CombatLog.h"

#include "anim/PoseSampler.h"

namespace selva::combat
{

namespace
{
// Default ON during active combat-feel iteration. The logging+disk-flush
// cost is real (Tracy showed selvaPerFrame max 77ms vs ~1ms baseline
// with debug active) — flip to false when not iterating on combat.
bool sEnabled = false;
FILE* sLogFile = nullptr;
} // namespace

bool isCombatDebugEnabled()
{
    return sEnabled;
}

FILE* openCombatLog()
{
    if (sLogFile == nullptr)
        sLogFile = std::fopen("combat-debug.log", "w");
    return sLogFile;
}

void closeCombatLog()
{
    if (sLogFile != nullptr)
    {
        std::fclose(sLogFile);
        sLogFile = nullptr;
    }
}

void setCombatDebugEnabled(bool enabled)
{
    if (sEnabled == enabled)
        return;
    sEnabled = enabled;
    if (sEnabled && sLogFile == nullptr)
    {
        sLogFile = std::fopen("combat-debug.log", "w");
        selva::anim::setSamplerDiagLog(sLogFile);
    }
    else if (!sEnabled && sLogFile != nullptr)
    {
        selva::anim::setSamplerDiagLog(nullptr);
        std::fclose(sLogFile);
        sLogFile = nullptr;
    }
}

void combatLog(const char* fmt, ...)
{
    if (!sEnabled)
        return;
    va_list args1;
    va_start(args1, fmt);
    va_list args2;
    va_copy(args2, args1);
    // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized): args1 is initialized by va_start above;
    // CSA can't model va_list init.
    std::vfprintf(stderr, fmt, args1);
    va_end(args1);
    if (sLogFile != nullptr)
    {
        // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized): args2 was initialized by va_copy.
        std::vfprintf(sLogFile, fmt, args2);
        std::fflush(sLogFile);
    }
    va_end(args2);
}

} // namespace selva::combat
