// Arduboy: perf hooks compile to nothing.
//
// The release build never measures itself — every flash byte is reserved
// for gameplay. Engine hot paths call perf_hook::charge() and the linker
// resolves it to this no-op TU. With -flto + -ffunction-sections, the
// empty function gets inlined and stripped entirely. Verify by checking
// post.disasm: `perf_hook::charge` should not appear as a symbol after
// link-time GC.

#include "perf_hook.h"

namespace perf_hook {

void charge(Op, u16) {
  // Intentionally empty.
}

}  // namespace perf_hook
