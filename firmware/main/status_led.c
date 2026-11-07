#include "status_led.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "LED";
static led_strip_handle_t s_strip;
static volatile status_led_state_t s_state = LED_NOT_JOINED;

void status_led_set(status_led_state_t s) { s_state = s; }

static void led_task(void *arg)
{
    /* Boot self-test (500 ms each): expect red, green, blue. This board's LED is RGB order (ETA_LED_ORDER_RGB=y). */
    static const uint8_t test[3][3] = { { 24, 0, 0 }, { 0, 24, 0 }, { 0, 0, 24 } };
    for (int i = 0; i < 3; i++) {
        led_strip_set_pixel(s_strip, 0, test[i][0], test[i][1], test[i][2]);
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    bool phase = false;
    for (;;) {
        uint8_t r = 0, g = 0, b = 0;
        switch (s_state) {
        case LED_NOT_JOINED: if (phase) r = 24; break;
        case LED_NO_BOILER:  r = 16; g = 10; break;
        case LED_OK:         g = 6; break;
        case LED_FAULT:      r = 24; break;
        }
        led_strip_set_pixel(s_strip, 0, r, g, b);
        led_strip_refresh(s_strip);
        phase = !phase;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void status_led_start(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_ETA_STATUS_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
#if CONFIG_ETA_LED_ORDER_RGB
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
#else
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
#endif
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip) != ESP_OK) {
        ESP_LOGW(TAG, "status LED init failed, continuing without it");
        return;
    }
    xTaskCreate(led_task, "status_led", 2048, NULL, 2, NULL);
}
