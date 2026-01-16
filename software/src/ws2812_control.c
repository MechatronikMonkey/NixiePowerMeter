#include "ws2812_control.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"

// Globale Mutex für Critical Section
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Timings für WS2812B angepasst für Direct Register Access
// 240MHz: 1 us = 240 cycles.
// T0H: 0.4us ~ 96 cycles
// Reduziert auf 50 Zyklen, um Overhead beim Schreiben/Funtionsaufruf zu kompensieren. 
// T0L: 0.85us ~ 204 cycles
// T1H: 0.8us ~ 192 cycles
// T1L: 0.45us ~ 108 cycles

#define CYCLES_T0H  50
#define CYCLES_T0L  200
#define CYCLES_T1H  192
#define CYCLES_T1L  100

static int s_led_pin = -1;
static uint32_t s_pin_mask = 0;
// Volatile pointers to registers
static volatile uint32_t *s_gpio_set_reg = NULL;
static volatile uint32_t *s_gpio_clr_reg = NULL;

void ws2812_init(int pin) {
    s_led_pin = pin;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);

    if (pin < 32) {
        s_pin_mask = (1UL << pin);
        s_gpio_set_reg = (volatile uint32_t *)GPIO_OUT_W1TS_REG;
        s_gpio_clr_reg = (volatile uint32_t *)GPIO_OUT_W1TC_REG;
    } else {
        s_pin_mask = (1UL << (pin - 32));
        s_gpio_set_reg = (volatile uint32_t *)GPIO_OUT1_W1TS_REG;
        s_gpio_clr_reg = (volatile uint32_t *)GPIO_OUT1_W1TC_REG;
    }
}

// Inline Assembler für präzise Delays (CPU Cycle Count)
static inline uint32_t get_ccount(void) {
    uint32_t ccount;
    asm volatile("rsr %0,ccount" : "=a" (ccount));
    return ccount;
}

static inline void delay_cycles(uint32_t cycles) {
    uint32_t start = get_ccount();
    while ((get_ccount() - start) < cycles) {
        // busy wait
    }
}

static void write_byte(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        if (b & (1 << i)) {
            // Bit 1: Lang High, Kurz Low
            *s_gpio_set_reg = s_pin_mask;
            delay_cycles(CYCLES_T1H);
            *s_gpio_clr_reg = s_pin_mask;
            delay_cycles(CYCLES_T1L);
        } else {
            // Bit 0: Kurz High, Lang Low
            *s_gpio_set_reg = s_pin_mask;
            delay_cycles(CYCLES_T0H);
            *s_gpio_clr_reg = s_pin_mask;
            delay_cycles(CYCLES_T0L);
        }
    }
}

void ws2812_write_leds(rgb_t *leds, int num_leds) {
    if (s_led_pin < 0 || !s_gpio_set_reg) return;

    portENTER_CRITICAL(&mux);
    
    // Reset Signal erzwingen (falls davor High war, sicherstellen dass es Low startet)
    *s_gpio_clr_reg = s_pin_mask;
    
    for (int i = 0; i < num_leds; i++) {
        write_byte(leds[i].g);
        write_byte(leds[i].r);
        write_byte(leds[i].b);
    }
    portEXIT_CRITICAL(&mux);
    
    ets_delay_us(300); // Latch / Reset (min 280us für manche neuen Chips)
}
