// SDL backend for uart_log: writes raw bytes to stdout. Pipe the process
// to a file to capture (`./mono-sdl.exe > trace.bin`). tools/perf_logger
// consumes the binary stream directly.

#include "uart_log.h"

#include <cstdio>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace uart_log {

void init() {
#ifdef _WIN32
  // Windows stdio defaults to text mode, which translates \n (0x0a) into
  // \r\n (0x0d 0x0a) on write. That's catastrophic for a binary record
  // stream — every 0x0a byte in any record gets a 0x0d inserted before
  // it, shifting everything downstream by one byte per occurrence and
  // making perf_logger's 7-byte alignment garbage. Force binary.
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  // stdout defaults to line-buffered on terminals and block-buffered on
  // pipes; we want unbuffered so the bytes hit disk as they're emitted
  // (important for crash-diagnostic logs and real-time inspection).
  std::setvbuf(stdout, nullptr, _IONBF, 0);
}

void write_byte(u8 b) {
  std::fputc((int)b, stdout);
}

}  // namespace uart_log
