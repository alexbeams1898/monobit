#include "debug/Flags.h"

namespace selva::debug
{

Flags& flags()
{
    static Flags g;
    return g;
}

} // namespace selva::debug
