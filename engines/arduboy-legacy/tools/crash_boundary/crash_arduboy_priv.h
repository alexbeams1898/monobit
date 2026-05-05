// Platform-private extras to crash.h, exposed only to the AVR main loop.
// The game layer must not pet or arm the watchdog directly — main owns
// the cadence.
//
// REDEPLOY: copy to platform/arduboy/crash_avr.h.

#pragma once

namespace crash {

// Enable the WDT with a 1-second timeout. Call once at boot, AFTER the
// init3 capture (which disables it) and AFTER display/clock init have
// finished any slow setup that might exceed the WDT window.
void arm_watchdog_1s();

// Pet the watchdog. Call once per frame from the main loop.
void pet_watchdog();

}  // namespace crash
