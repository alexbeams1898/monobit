// SSD1306 + hardware SPI driver, factored out of the day-one main.cpp.
//
// Pin map per the Arduboy schematic:
//   OLED CS   -> PD6  (active low)
//   OLED DC   -> PD4  (low = command, high = data)
//   OLED RST  -> PD7  (active low)
//   OLED SCK  -> PB1  (hardware SPI clock)
//   OLED MOSI -> PB2  (hardware SPI data out)
//   SPI SS    -> PB0  (must be configured as output for master mode)

#include "display.h"
#include "framebuffer.h"

#include <avr/io.h>
#include <util/delay.h>

namespace {

inline void cs_low() {
  PORTD &= ~(1 << 6);
}
inline void cs_high() {
  PORTD |= (1 << 6);
}
inline void dc_low() {
  PORTD &= ~(1 << 4);
}
inline void dc_high() {
  PORTD |= (1 << 4);
}
inline void rst_low() {
  PORTD &= ~(1 << 7);
}
inline void rst_high() {
  PORTD |= (1 << 7);
}

inline void spi_write(uint8_t b) {
  SPDR = b;
  while (!(SPSR & (1 << SPIF))) {}
}

void oled_cmd(uint8_t c) {
  dc_low();
  cs_low();
  spi_write(c);
  cs_high();
}

}  // namespace

namespace display {

void init() {
  // SPI master at fosc/2.
  DDRB |= (1 << 0) | (1 << 1) | (1 << 2);
  SPCR = (1 << SPE) | (1 << MSTR);
  SPSR = (1 << SPI2X);

  // Control pins as outputs.
  DDRD |= (1 << 4) | (1 << 6) | (1 << 7);

  // Hardware reset.
  rst_high();
  _delay_ms(1);
  rst_low();
  _delay_ms(10);
  rst_high();
  _delay_ms(10);

  // SSD1306 init sequence.
  oled_cmd(0xAE);
  oled_cmd(0xD5);
  oled_cmd(0xF0);
  oled_cmd(0xA8);
  oled_cmd(0x3F);
  oled_cmd(0xD3);
  oled_cmd(0x00);
  oled_cmd(0x40);
  oled_cmd(0x8D);
  oled_cmd(0x14);
  oled_cmd(0x20);
  oled_cmd(0x00);
  oled_cmd(0xA1);
  oled_cmd(0xC8);
  oled_cmd(0xDA);
  oled_cmd(0x12);
  oled_cmd(0x81);
  oled_cmd(0xCF);
  oled_cmd(0xD9);
  oled_cmd(0xF1);
  oled_cmd(0xDB);
  oled_cmd(0x40);
  oled_cmd(0xA4);
  oled_cmd(0xA6);
  oled_cmd(0xAF);
}

void flush() {
  // Address the whole 128x64 region.
  oled_cmd(0x21);
  oled_cmd(0x00);
  oled_cmd(0x7F);
  oled_cmd(0x22);
  oled_cmd(0x00);
  oled_cmd(0x07);

  dc_high();
  cs_low();
  for (uint16_t i = 0; i < fb::SIZE; ++i) {
    spi_write(fb::buffer[i]);
  }
  cs_high();
}

}  // namespace display
