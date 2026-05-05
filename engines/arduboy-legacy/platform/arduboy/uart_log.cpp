// Arduboy UART-log backend: USART1 as raw-binary emitter for perf-logger.
//
// USART1 at U2X1=1 gives the finest baud granularity. We target 500000
// baud because:
//   UBRR1 = F_CPU / (8 * baud) - 1 = 16e6 / 4e6 - 1 = 3  (exact, no error)
// 500000 baud = 50000 bytes/sec effective with 8N1 framing. Our perf
// logger emits 3 bytes/frame * 60 fps = 180 bytes/sec — we have a >250x
// margin, so write_byte is a simple polling send with no buffer needed.
//
// We drive TX only (PD3 -> ATmega32u4 USART1_TX). RX stays disabled.

#include "uart_log.h"

#include <avr/io.h>

namespace uart_log {

namespace {
bool initialized = false;

void init_once() {
  // 500000 baud, U2X1=1 mode.
  UBRR1H = 0;
  UBRR1L = 3;
  UCSR1A = (1 << U2X1);
  // TX only, 8 data bits, no parity, 1 stop bit (8N1 — UCSZ1[1:0] = 0b11).
  UCSR1B      = (1 << TXEN1);
  UCSR1C      = (1 << UCSZ11) | (1 << UCSZ10);
  initialized = true;
}

}  // namespace

void init() {
  if (!initialized) init_once();
}

void write_byte(u8 b) {
  if (!initialized) init_once();
  // Poll UDRE1 (data register empty) then shift the byte out. UDRE1 is
  // set when the transmit buffer can accept a new byte.
  while (!(UCSR1A & (1 << UDRE1))) {}
  UDR1 = b;
}

}  // namespace uart_log
