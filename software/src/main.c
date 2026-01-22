#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "driver/gpio.h"
#include "ws2812_control.h"

// =========================================================
// KONFIGURATION (HIER ANPASSEN)
// =========================================================

// --- Hardware Pins (an ESP32-S3 Zero anpassen) ---
#define LED_PIN             GPIO_NUM_21 // WS2812B
#define ADC_PIN             GPIO_NUM_6  // ADC1 Channel 5
#define BUTTON_PIN          GPIO_NUM_0  // Boot Button (Input)
#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_5

// Shift Register (74HC595)
#define SR_DATA_PIN         GPIO_NUM_10 // DS (SER)
#define SR_OE_PIN           GPIO_NUM_9  // OE (Output Enable, low active)
#define SR_LATCH_PIN        GPIO_NUM_8  // ST_CP (RCLK) - Storage Register Clock
#define SR_CLOCK_PIN        GPIO_NUM_7  // SH_CP (SRCLK) - Shift Register Clock
#define SR_MR_PIN           GPIO_NUM_11 // MR (Master Reset, low active)

// --- Mess-Parameter ---
#define SAMPLE_FREQ_HZ      80000       // 80 kHz Sampling
#define ADC_CONV_MODE       ADC_CONV_SINGLE_UNIT_1
#define ADC_ATTEN           ADC_ATTEN_DB_12 // 11/12dB Attenuation (bis ca. 3.1V messbar)
#define ADC_OUTPUT_TYPE     ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define ADC_GET_CHANNEL(p_data)     ((p_data)->type2.channel)
#define ADC_GET_DATA(p_data)        ((p_data)->type2.data)

// --- Physik & Kalibrierung ---
// Sensor: 2.5V +/- 2.0V = +/- 6A. (0.5V = -6A, 4.5V = +6A)
// Empfindlichkeit beim Sensor-Ausgang: 2.0V / 6A = 0.3333 V/A.
// Spannungsteiler: R_TOP=1200, R_BOT=3300 -> Faktor = R_BOT / (R_TOP + R_BOT)
// Bei 4.5V Input (Max Sensor) und 1k2/3k3 kommen exakt 3.3V an.
#define R_TOP_OHM           1200.0f
#define R_BOT_OHM           3300.0f
#define DIVIDER_FACTOR      (R_BOT_OHM / (R_TOP_OHM + R_BOT_OHM))

#define SENSOR_OFFSET_V     2.50f       // Nullpunkt des Sensors (0A)
#define SENSOR_SENSITIVITY  0.3333f     // Volt pro Ampere am Sensor-Ausgang

// ADC Referenzspannung (nominal 3.3V, muss kalibriert werden für Präzision)
#define ADC_REF_V           3.3f
#define ADC_RES_BITS        12
#define ADC_MAX_VAL         4095.0f

// gleitender Mittelwert
#define AVG_WINDOW_SIZE     10          // Anzahl der 50ms Pakete für 500ms Mittelwert (ca.)
// Ausgabe Intervall
#define OUTPUT_INTERVAL_MS  500

// DMA Puffer Größe
#define READ_LEN            1024        // Bytes pro DMA Read Cycle (muss Vielfaches von SOC_ADC_DIGI_DATA_BYTES_PER_CONV sein)

static const char *TAG = "RMS_METER";

// Globale Variablen für Tasks
static TaskHandle_t s_task_handle;
static adc_continuous_handle_t handle = NULL;

// RMS Akkumulatoren
typedef struct {
    double sum_squares;
    double sum_volts;   // Für DC Diagnose
    uint64_t sum_raw;   // Für Raw Value Diagnose
    uint32_t count;
} adc_chunk_result_t;

QueueHandle_t result_queue;

// --- Modes ---
typedef struct {
    float factor;
    uint8_t r, g, b;
} mode_config_t;

static const mode_config_t MODES[] = {
    {4.0f,  20, 0, 20},  // 0: Factor 4 (Pink)
    {8.0f,  30, 8, 0},   // 1: Factor 8 (Orange)
    {16.0f, 0, 0, 30}    // 2: Factor 16 (Blue)
};
static int current_mode_idx = 0; // Default Factor 4

// =========================================================
// LED STATUS SERVICE (WS2812B)
// =========================================================
// static led_strip_handle_t led_strip; // replaced by custom implementation

typedef enum {
    LED_OFF,
    LED_STATIC,
    LED_BLINK_SLOW,
    LED_BLINK_FAST
} led_mode_t;

typedef struct {
    led_mode_t mode;
    uint8_t r, g, b;
} led_cmd_t;

QueueHandle_t led_queue;

void set_led_status(led_mode_t mode, uint8_t r, uint8_t g, uint8_t b) {
    if (led_queue == NULL) return;
    led_cmd_t cmd = { .mode = mode, .r = r, .g = g, .b = b };
    xQueueOverwrite(led_queue, &cmd);
}

void led_task(void *pvParameters) {
    led_cmd_t current_state = { .mode = LED_BLINK_SLOW, .r = 0, .g = 20, .b = 0 };
    led_cmd_t new_cmd;
    bool led_on = false;
    TickType_t last_toggle = 0;
    
    // Buffer for 1 LED
    rgb_t led_color = {0, 0, 0};

    while(1) {
        if (xQueueReceive(led_queue, &new_cmd, 0) == pdTRUE) {
            current_state = new_cmd;
            // Force Update immediately
            if (current_state.mode == LED_STATIC) {
                led_color.r = current_state.r;
                led_color.g = current_state.g;
                led_color.b = current_state.b;
                ws2812_write_leds(&led_color, 1);
                led_on = true;
            } else if (current_state.mode == LED_OFF) {
                led_color.r = 0; led_color.g = 0; led_color.b = 0;
                ws2812_write_leds(&led_color, 1);
                led_on = false;
            }
        }

        if (current_state.mode == LED_BLINK_SLOW || current_state.mode == LED_BLINK_FAST) {
            uint32_t interval = (current_state.mode == LED_BLINK_FAST) ? pdMS_TO_TICKS(100) : pdMS_TO_TICKS(500);
            TickType_t now = xTaskGetTickCount();
            if ((now - last_toggle) >= interval) {
                led_on = !led_on;
                if (led_on) {
                    led_color.r = current_state.r;
                    led_color.g = current_state.g;
                    led_color.b = current_state.b;
                } else {
                    led_color.r = 0; led_color.g = 0; led_color.b = 0;
                }
                ws2812_write_leds(&led_color, 1);
                last_toggle = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void init_led() {
    ws2812_init(LED_PIN);
    
    // Clear LED
    rgb_t black = {0,0,0};
    ws2812_write_leds(&black, 1);
    
    led_queue = xQueueCreate(1, sizeof(led_cmd_t));
    
    xTaskCreate(led_task, "led_task", 2048, NULL, 1, NULL);
    
    // Default Status: Grün Static (Test, weil blinken evtl. Timing probleme zeigte)
    // R=0, G=20, B=0
    set_led_status(LED_STATIC, 0, 20, 0); 
}

// =========================================================
// SHIFT REGISTER FUNKTIONEN
// =========================================================
void init_shift_register() {
    gpio_reset_pin(SR_DATA_PIN);
    gpio_reset_pin(SR_CLOCK_PIN);
    gpio_reset_pin(SR_LATCH_PIN);
    gpio_reset_pin(SR_OE_PIN);
    gpio_reset_pin(SR_MR_PIN);

    gpio_set_direction(SR_DATA_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SR_CLOCK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SR_LATCH_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SR_OE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SR_MR_PIN, GPIO_MODE_OUTPUT);
    
    // Initial State
    // MR (Master Reset) - Low active. L resets Shift Register.
    // wir resetten kurz und setzen dann auf High (Operation Mode).
    gpio_set_level(SR_MR_PIN, 0); 
    // Wait tiny bit? (ESP GPIO is slow enough usually, but let's do explicit set)
    gpio_set_level(SR_CLOCK_PIN, 0);
    gpio_set_level(SR_LATCH_PIN, 0);
    
    // Release Reset
    gpio_set_level(SR_MR_PIN, 1);

    // OE (Output Enable) - Low active.
    // Low = Outputs enabled (Visible). High = High-Z (Off).
    gpio_set_level(SR_OE_PIN, 0); 
}

void shift_out_byte(uint8_t data) {
    // MSB First
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(SR_CLOCK_PIN, 0);
        gpio_set_level(SR_DATA_PIN, (data >> i) & 1);
        gpio_set_level(SR_CLOCK_PIN, 1); // Rising Edge -> Shift
    }
    gpio_set_level(SR_CLOCK_PIN, 0); // Idle Low
}

// Schreibt 3 Bytes und aktiviert Latch
// Order: Byte für Reg3 (First out), Byte für Reg2, Byte für Reg1 (Last out)
void write_nixie_register(uint8_t r1, uint8_t r2, uint8_t r3) {
    // Chain: MC -> Reg1 -> Reg2 -> Reg3
    // Wir müssen zuerst die Daten für Reg3 rausschieben, dann Reg2, dann Reg1.
    
    shift_out_byte(r3);
    shift_out_byte(r2);
    shift_out_byte(r1);

    // Latch toggle (Rising Edge transfers Shift Reg -> Storage Reg)
    gpio_set_level(SR_LATCH_PIN, 0);
    gpio_set_level(SR_LATCH_PIN, 1);
    gpio_set_level(SR_LATCH_PIN, 0);
}

void update_display(int number) {
    if (number > 99) number = 99;
    if (number < 0) number = 0;

    int tens = number / 10;
    int ones = number % 10;

    uint8_t reg1 = 0;
    uint8_t reg2 = 0;
    uint8_t reg3 = 0;

    /*
      Mapping Korrektur (Basierend auf User Input):
      Q0 (Bit 0) ist bei allen Registern NICHT angeschlossen (Layout bedingt).
      Q1 entspricht also Bit 1 (1<<1), usw.

      Zehner-Stelle:
      1: Reg1 Q1  2: Reg1 Q2  3: Reg1 Q3  4: Reg1 Q4
      5: Reg1 Q5  6: Reg1 Q6  7: Reg1 Q7
      8: Reg2 Q1  9: Reg2 Q2  0: Reg2 Q3
      (Reg2 Q0 ist leer/übersprungen)

      Einer-Stelle:
      1: Reg2 Q4  2: Reg2 Q5  3: Reg2 Q6  4: Reg2 Q7
      5: Reg3 Q1  6: Reg3 Q2  7: Reg3 Q3  8: Reg3 Q4  9: Reg3 Q5  0: Reg3 Q6
      (Reg3 Q0 ist leer/übersprungen)
    */

    // Zehner Mapping
    switch(tens) {
        case 1: reg1 |= (1<<1); break;
        case 2: reg1 |= (1<<2); break;
        case 3: reg1 |= (1<<3); break;
        case 4: reg1 |= (1<<4); break;
        case 5: reg1 |= (1<<5); break;
        case 6: reg1 |= (1<<6); break;
        case 7: reg1 |= (1<<7); break;
        case 8: reg2 |= (1<<1); break;
        case 9: reg2 |= (1<<2); break;
        case 0: reg2 |= (1<<3); break;
    }

    // Einer Mapping
    switch(ones) {
        case 1: reg2 |= (1<<4); break;
        case 2: reg2 |= (1<<5); break;
        case 3: reg2 |= (1<<6); break;
        case 4: reg2 |= (1<<7); break;
        case 5: reg3 |= (1<<1); break;
        case 6: reg3 |= (1<<2); break;
        case 7: reg3 |= (1<<3); break;
        case 8: reg3 |= (1<<4); break;
        case 9: reg3 |= (1<<5); break;
        case 0: reg3 |= (1<<6); break;
    }

    write_nixie_register(reg1, reg2, reg3);
}

// =========================================================
// ADC & DSP LOGIK
// =========================================================

// Hilfsfunktion: Raw ADC Wert zu Strom (Ampere)
float raw_to_ampere(uint32_t raw_val) {
    // 1. Raw zu ADC-Pin Spannung
    float v_pin = ((float)raw_val / ADC_MAX_VAL) * ADC_REF_V; // Grobe Näherung, besser curve fitting
    
    // 2. Rückrechnen Spannungsteiler -> Sensor Ausgangsspannung
    float v_sensor = v_pin / DIVIDER_FACTOR;

    // 3. Offset entfernen (AC Kopplung im mathematischen Sinne)
    float v_delta = v_sensor - SENSOR_OFFSET_V;

    // 4. In Ampere umrechnen
    float ampere = v_delta / SENSOR_SENSITIVITY;
    
    return ampere;
}

// Task: Liest DMA Puffer und integriert Summe der Quadrate
void adc_processing_task(void *pvParameters) {
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[READ_LEN] = {0};
    memset(result, 0xcc, READ_LEN);

    adc_chunk_result_t chunk_res;
    
    while (1) {
        // Blockiert, bis genügend Daten im DMA Buffer sind (Timeout MAX)
        ret = adc_continuous_read(handle, result, READ_LEN, &ret_num, portMAX_DELAY);
        
        if (ret == ESP_OK) {
            chunk_res.sum_squares = 0;
            chunk_res.sum_volts = 0;
            chunk_res.sum_raw = 0;
            chunk_res.count = 0;

            for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
                uint32_t chan_num = ADC_GET_CHANNEL(p);
                uint32_t data = ADC_GET_DATA(p);
                
                if (chan_num == ADC_CHANNEL) {
                    float current_a = raw_to_ampere(data);
                    // RMS integration: Summe(x^2)
                    chunk_res.sum_squares += (current_a * current_a);
                    // Debug Stats
                    chunk_res.sum_volts += current_a; 
                    chunk_res.sum_raw += data;

                    chunk_res.count++;
                }
            }
            
            // Ergebnis an Haupt-Task senden zur Mittelwertbildung
            if (chunk_res.count > 0) {
                if(xQueueSend(result_queue, &chunk_res, 0) != pdTRUE) {
                    // Queue Full -> Processing too slow!
                    // Error: Yellow Fast Blink
                    set_led_status(LED_BLINK_FAST, 20, 20, 0); 
                }
            }

        } else if (ret == ESP_ERR_TIMEOUT) {
            // Sollte bei Continuous Mode mit portMAX_DELAY nicht passieren
            ESP_LOGW(TAG, "ADC Read Timeout");
            set_led_status(LED_BLINK_FAST, 20, 0, 0); // ROT
        }
    }
}

// Task: Gleitender Mittelwert & Ausgabe
void main_logic_task(void *pvParameters) {
    // Init Button
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    
    // Button Logic Vars
    int last_btn_level = 1;
    TickType_t btn_press_start = 0;
    bool btn_processed_long = false; // Prevents Short Press if Long Press occurred

    TickType_t mode_feedback_end_time = 0;
    bool is_test_mode = false;
    int test_counter = 0;

    adc_chunk_result_t chunk_in;
    
    double total_sum_sq = 0;
    double total_sum_volts = 0; // Ampere eigentlich
    uint64_t total_sum_raw = 0;
    uint64_t total_samples = 0;
    
    TickType_t last_output_time = xTaskGetTickCount();
    const TickType_t output_period = pdMS_TO_TICKS(OUTPUT_INTERVAL_MS);

    // Monitoring
    uint64_t total_samples_since_boot = 0;
    TickType_t last_monitor_time = xTaskGetTickCount();

    while(1) {
        // Button Polling (Active Low)
        int btn_level = gpio_get_level(BUTTON_PIN);
        TickType_t now = xTaskGetTickCount();

        if (btn_level == 0) {
            // Button is currently pressed
            if (last_btn_level == 1) {
                // Edge: Pressed just now
                btn_press_start = now;
                btn_processed_long = false;
            } else {
                // Held down
                if (!btn_processed_long && (now - btn_press_start) > pdMS_TO_TICKS(3000)) {
                    // Long Press Detected (3s) -> Toggle Test Mode
                    is_test_mode = !is_test_mode;
                    btn_processed_long = true; // Mark as handled
                    
                    ESP_LOGI(TAG, "Test Mode: %s", is_test_mode ? "ON" : "OFF");
                    
                    if (is_test_mode) {
                        set_led_status(LED_BLINK_FAST, 255, 255, 255); // White Fast Blink
                        test_counter = 0;
                    } else {
                        // Restore Status by cleaning Queue (Monitoring will pick up)
                        update_display(0);
                    }
                }
            }
        } else {
            // Button is released
            if (last_btn_level == 0) {
                // Edge: Released
                if (!btn_processed_long) {
                    // Short Press -> Switch Factor Mode
                    // Only switch if NOT in test mode (optional, but cleaner)
                    if (!is_test_mode) {
                        current_mode_idx = (current_mode_idx + 1) % 3;
                        const mode_config_t *m = &MODES[current_mode_idx];
                        set_led_status(LED_STATIC, m->r, m->g, m->b);
                        mode_feedback_end_time = now + pdMS_TO_TICKS(2000);
                        ESP_LOGI(TAG, "Mode changed: Factor %.0f", m->factor);
                    }
                }
            }
        }
        last_btn_level = btn_level;


        // Hole alle verfügbaren Chunks aus der Queue und integriere sie
        // Wir begrenzen die Schleife nicht, um die Queue schnell leer zu machen
        while(xQueueReceive(result_queue, &chunk_in, pdMS_TO_TICKS(10)) == pdTRUE) {
            total_sum_sq += chunk_in.sum_squares;
            total_sum_volts += chunk_in.sum_volts;
            total_sum_raw += chunk_in.sum_raw;
            total_samples += chunk_in.count;
            total_samples_since_boot += chunk_in.count;
        }

        now = xTaskGetTickCount();

        // 3. Display Update
        if ((now - last_output_time) >= output_period) {
            
            if (is_test_mode) {
                // TEST MODE: Counter 00-99
                update_display(test_counter);
                ESP_LOGI(TAG, "TEST MODE: %d", test_counter);
                test_counter++;
                if (test_counter > 99) test_counter = 0;
                
                // Keep LED blinking fast in Test Mode
                set_led_status(LED_BLINK_FAST, 50, 50, 50); // White fast
                
            } else {
                // NORMAL MODE
                float rms_amperes = 0.0f;
                float dc_avg_amp = 0.0f;
                float raw_avg = 0.0f;

                if (total_samples > 0) {
                    rms_amperes = sqrtf(total_sum_sq / total_samples);
                    dc_avg_amp = (float)(total_sum_volts / total_samples);
                    raw_avg = (float)total_sum_raw / total_samples;
                }

                // Apply Factor based on Mode
                float factor = MODES[current_mode_idx].factor;
                
                // Calculate Power in Watts (P = I^2 * R)
                float power_watts = (rms_amperes * rms_amperes) * factor;
                int display_val = (int)(power_watts + 0.5f);
                
                // Detailed Logging for Debugging the "30" issue
                // Wenn raw_avg ~0 ist, hat der Sensor 0V output (Pin auf GND).
                // Sollte aber bei 2.5V Sensor ~2270 (bei 12 bit und divider) liegen.
                ESP_LOGI(TAG, "RMS: %.3f A | DC: %.3f A | RawAvg: %.1f | Dis: %d", 
                         rms_amperes, dc_avg_amp, raw_avg, display_val);
                
                update_display(display_val);
            }

            // Reset für nächstes 500ms Intervall
            total_sum_sq = 0;
            total_sum_volts = 0;
            total_sum_raw = 0;
            total_samples = 0;
            last_output_time = now;
        }

        // Überprüfung der Sampling Rate (Monitoring) alle 2 Sekunden
        if ((now - last_monitor_time) >= pdMS_TO_TICKS(2000)) {
             // Erwartet: 80k samples/sec * 2s = 160k samples
             // Wir erlauben etwas Toleranz (z.B. +/- 10%)
             // Da wir hier resetten, berechnen wir die Differenz
             // Vereinfachung: Hier nur logging, da counter fortlaufend
             static uint64_t last_total_boot = 0;
             uint64_t diff = total_samples_since_boot - last_total_boot;
             last_total_boot = total_samples_since_boot;

             float measured_freq = diff / 2.0f;
             ESP_LOGI(TAG, "Input Freq: %.0f Hz (Target: %d)", measured_freq, SAMPLE_FREQ_HZ);

             // Update Status LED ONLY IF we are not in Mode-Feedback phase AND not in Test Mode
             if (now > mode_feedback_end_time && !is_test_mode) {
                 if (measured_freq < (SAMPLE_FREQ_HZ * 0.9f)) {
                     // Warning: Sampling zu langsam (Sample Loss?)
                     set_led_status(LED_BLINK_SLOW, 20, 20, 0); // GELB
                 } else {
                     // OK: Grün Blinkend (RUNNING)
                     set_led_status(LED_BLINK_SLOW, 0, 20, 0); 
                 }
             }

             last_monitor_time = now;
        }
    }
}


// =========================================================
// INITIALISIERUNG
// =========================================================
static void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle) {
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 4096,
        .conv_frame_size = READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, out_handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_MODE,
        .format = ADC_OUTPUT_TYPE,
    };

    adc_digi_pattern_config_t adc_pattern[1] = {0};
    dig_cfg.pattern_num = 1;
    adc_pattern[0].atten = ADC_ATTEN;
    adc_pattern[0].channel = channel[0] & 0x7;
    adc_pattern[0].unit = ADC_UNIT;
    adc_pattern[0].bit_width = ADC_RES_BITS;

    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(*out_handle, &dig_cfg));
}

void app_main(void) {
    // 1. Queue Init (Increased size to buffer ~160ms of data)
    result_queue = xQueueCreate(50, sizeof(adc_chunk_result_t));

    // 2. IO Init
    init_shift_register();
    init_led();

    // 3. ADC Init
    adc_channel_t channel[1] = {ADC_CHANNEL};
    continuous_adc_init(channel, sizeof(channel) / sizeof(adc_channel_t), &handle);
    ESP_ERROR_CHECK(adc_continuous_start(handle));

    // 4. Tasks starten
    // Core 1 für DSP/Processing
    xTaskCreatePinnedToCore(adc_processing_task, "adc_proc", 4096, NULL, 5, &s_task_handle, 1);
    
    // Core 0 oder 1 für Main Logic
    xTaskCreate(main_logic_task, "main_logic", 4096, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "System gestartet. ADC läuft @ %d Hz", SAMPLE_FREQ_HZ);
}
