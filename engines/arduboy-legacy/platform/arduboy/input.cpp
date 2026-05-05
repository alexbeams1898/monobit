// Arduboy button reader.
//
// Pin map per the Arduboy schematic (all active LOW: pin reads 0 when pressed):
//   UP    -> PF7
//   DOWN  -> PF4
//   LEFT  -> PF5
//   RIGHT -> PF6
//   A     -> PE6
//   B     -> PB4
//
// The chip's internal pull-ups hold the lines high when buttons are released,
// so we just enable pull-ups (set DDR=input, PORT=high) and read.

#include "input.h"

#include <avr/io.h>

namespace {

uint8_t current  = 0;  // bit per button, set if held this frame
uint8_t previous = 0;  // bit per button, set if held last frame

uint8_t read_raw() {
  uint8_t s = 0;
  // PINF reads PORTF inputs. Buttons pull the line low -> invert for "held".
  if (!(PINF & (1 << 7))) s |= (1 << input::UP);
  if (!(PINF & (1 << 4))) s |= (1 << input::DOWN);
  if (!(PINF & (1 << 5))) s |= (1 << input::LEFT);
  if (!(PINF & (1 << 6))) s |= (1 << input::RIGHT);
  if (!(PINE & (1 << 6))) s |= (1 << input::A);
  if (!(PINB & (1 << 4))) s |= (1 << input::B);
  return s;
}

bool initialized = false;
void init_once() {
  // Configure as inputs with pull-ups enabled.
  DDRF &= ~((1 << 7) | (1 << 6) | (1 << 5) | (1 << 4));
  PORTF |= ((1 << 7) | (1 << 6) | (1 << 5) | (1 << 4));
  DDRE &= ~(1 << 6);
  PORTE |= (1 << 6);
  DDRB &= ~(1 << 4);
  PORTB |= (1 << 4);
  initialized = true;
}

}  // namespace

namespace input {

void poll() {
  if (!initialized) init_once();
  previous = current;
  current  = read_raw();
}

bool held(Button b) {
  return (current >> b) & 1;
}
bool pressed(Button b) {
  return ((current & ~previous) >> b) & 1;
}

}  // namespace input
