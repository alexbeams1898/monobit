# dbg_trace — runtime tracepoint emitter and parser

Plant `dbg::trace(TAG)` or `dbg::trace(TAG, value)` at any decision
point you want visibility into. Build with `-DDBG_TRACE` (Makefile or
CMake), run, capture USART1 (Arduboy → Ardens serial console) or
stdout (SDL), feed the bytes to `parse.py`, read the timeline of
events.

The header is `engine/dbg.h`. Records are 5 bytes each, framed so a
truncated or jittered stream can resync. Disabled by default — the
function bodies are empty inline stubs the compiler eliminates, so
shipping code carries zero cost. Enable per-session.

## Why this exists

The constraint surface in mono — 28 KB flash + 2.5 KB RAM + bank
paging + FX SPI + audio ISR + watchdog + scene paging — is too dense
to debug by source-reading. Theory-from-source produces wrong
hypotheses regularly. `dbg::trace` is the always-available answer:
plant a marker, run the broken path, read what actually happened.

Use this BEFORE proposing a fix. Always.

## Usage

### Enable for a session

Arduboy:
```
make all DBG_TRACE=1
make ardens-fresh
```

(Until the Makefile is taught the `DBG_TRACE` knob, edit
`platform/arduboy/uart_log.cpp` or just `#define DBG_TRACE 1` at the
top of the .cpp file you're tracing — works for one TU only.)

SDL:
```
cmake -B build-sdl -DDBG_TRACE=1
cmake --build build-sdl
build-sdl/bin/mono-sdl.exe > trace.bin
```

### Plant tracepoints

```cpp
#include "dbg.h"

void some_function() {
  dbg::trace(0x01);                         // I reached point A
  dbg::trace(0x02, current_sector);         // sector value at point B
  dbg::trace(0x03, (u16)result_status);     // status code at point C
}
```

Tag conventions:
- `0x01..0x7F` — ad-hoc per-session, document in the file you plant
  them. Recycle freely.
- `0x80..0xFE` — long-lived. If you wire one up meant to survive
  across sessions, document it in `engine/dbg.h` and in this README's
  registry below.
- `0x00`, `0xFF`, `0xAA` reserved (framing / sentinel).

### Capture the trace

**Ardens:** open the **Serial Console** window (View menu). Bytes
scroll as the chip emits them. Save the buffer to a file when done.

**SDL:** redirect stdout. Run `build-sdl/bin/mono-sdl.exe > trace.bin`.

### Decode

```
python tools/dbg_trace/parse.py trace.bin
```

Output is one line per record:
```
[  0] tag=0x12 value=0x004F  ('reached commit_slot_change pre-erase')
[  1] tag=0x13 value=0x1000  ('dest_off')
[  2] tag=0x14 value=0x0001  ('save_erase_sector returned: true')
...
```

Pass `--annotate annotations.txt` to inject human-readable strings
per tag (otherwise just the hex). The annotations file is one line
per tag: `0x12 reached commit_slot_change pre-erase`.

## Long-lived tag registry

(empty for now — first long-lived tag will land here)

## Design notes

- 5-byte record (vs 1- or 2-byte): framing + tag + u16 value + XOR
  checksum. Catches single-bit corruption, lets the parser resync
  after stream cuts.
- 0xAA framing byte chosen because it's a single-bit-error from 0x00
  and 0xFF (less likely to be confused with erased flash or pulled-up
  MISO).
- u16 value: covers most things you want to log (addresses, status
  codes, sector offsets, SP). u32 would double cost and rarely be
  needed at this level. If you need u32, emit two records.
- Inline header, no .cpp: GCC eliminates the call entirely when
  disabled. No link-time guard needed.
- `dbg::trace` not `dbg::log`: "log" connotes structured persistent
  records; "trace" connotes ephemeral execution-flow events.

## Caveats

- Don't call from inside an ISR. The audio ISR fires at 16 kHz; even
  one trace per ISR fire would saturate the UART. Trace from update/
  draw / per-event paths only.
- The Ardens serial console can lose bytes if the receive buffer
  fills faster than the host renders. Save to file ASAP if the trace
  is dense.
- On Arduboy, UART writes block briefly while the byte shifts out.
  At 500_000 baud that's ~16 µs/byte = 80 µs per 5-byte record. Three
  records per frame is ~240 µs (1.4% of a 16.67 ms frame). Acceptable.
  Hundreds of records per frame is not.
