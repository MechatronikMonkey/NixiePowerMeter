#include "ws2812_control.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "WS2812";
static led_strip_handle_t led_strip;

void ws2812_init(int pin) {
    ESP_LOGI(TAG, "Initializing WS2812 via RMT on Pin %d", pin);

    // LED Strip Config
    led_strip_config_t strip_config = {
        .strip_gpio_num = pin,
        .max_leds = 1, // Wir nutzen nur 1 LED
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, 
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    // RMT Backend Config
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, 
        .resolution_hz = 10 * 1000 * 1000, // 10MHz Resolution
        .flags.with_dma = false, 
    };

    // Create LED Strip Object
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    led_strip_clear(led_strip);
}

void ws2812_write_leds(rgb_t *leds, int num_leds) {
    if (!led_strip) return;

    for (int i = 0; i < num_leds; i++) {
        led_strip_set_pixel(led_strip, i, leds[i].r, leds[i].g, leds[i].b);
    }
    led_strip_refresh(led_strip);
}

