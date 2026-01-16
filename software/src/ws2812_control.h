#ifndef WS2812_CONTROL_H
#define WS2812_CONTROL_H

#include <stdint.h>
#include "driver/gpio.h"

// Einfache Struktur für Farbe
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

void ws2812_init(int pin);
void ws2812_write_leds(rgb_t *leds, int num_leds);

#endif
