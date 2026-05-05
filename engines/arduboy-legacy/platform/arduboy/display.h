// Arduboy SSD1306 + SPI driver.
// Engine code never includes this — only main.cpp wires it up.

#pragma once

namespace display {

void init();   // SPI init + SSD1306 power-on sequence.
void flush();  // Push the engine's framebuffer to the OLED.

}  // namespace display
