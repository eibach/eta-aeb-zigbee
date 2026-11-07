#include "eta_link.h"

#include <math.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "eta_points.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "ETA_LINK";

#define ETA_UART_NUM   ((uart_port_t)CONFIG_ETA_UART_NUM)
#define ALIVE_TIMEOUT_US (3LL * CONFIG_ETA_REFRESH_S * 1000000LL)

static const eta_link_cb_t *s_cb;
static volatile int64_t s_last_data_us = -1;
static volatile bool s_alive;
static uint32_t s_rx_total; /* diagnostics: bytes received on the UART since boot */

bool eta_link_alive(void) { return s_alive; }

static void set_alive(bool alive)
{
    if (alive != s_alive) {
        s_alive = alive;
        ESP_LOGI(TAG, "boiler link %s", alive ? "UP" : "DOWN");
        if (s_cb && s_cb->on_link) s_cb->on_link(alive);
    }
}

static void __attribute__((unused)) check_timeout(void)
{
    int64_t now = esp_timer_get_time();
    if (s_alive && (now - s_last_data_us) > ALIVE_TIMEOUT_US) set_alive(false);
}

#if CONFIG_ETA_SIMULATE
/* ---------------- simulator: no UART, plausible fake values ---------------- */

bool eta_link_send_command(uint8_t ih_bit)
{
    ESP_LOGW(TAG, "[SIM] would send IH 0x%02x", ih_bit);
    return true;
}

static void sim_task(void *arg)
{
    float t = 0;
    for (;;) {
        eta_record_t rec[ETA_MAX_POLL_ITEMS];
        int n = 0;
        for (size_t i = 0; i < eta_points_count && n < ETA_MAX_POLL_ITEMS; i++) {
            const eta_point_t *p = &eta_points[i];
            float v;
            switch (p->index) {
            case ETA_AEL_BOILER:          v = 62.0f + 8.0f * sinf(t); break;
            case ETA_AEL_FLUE_GAS:        v = 110.0f + 20.0f * sinf(t); break;
            case ETA_AEL_BUFFER_TOP:      v = 58.0f + 3.0f * sinf(t / 2); break;
            case ETA_AEL_BUFFER_BOTTOM:   v = 41.0f + 3.0f * sinf(t / 2); break;
            case ETA_AEL_DHW_TANK:        v = 52.0f; break;
            case ETA_AEL_DHW_TANK_BOTTOM: v = 38.5f; break;
            case ETA_AEL_OUTSIDE:         v = 6.3f + 1.5f * sinf(t / 5); break;
            case ETA_AEL_FLOW_DIRECT:     v = 45.0f; break;
            case ETA_AEL_FLOW_MIXER0:     v = 38.0f; break;
            case ETA_AEL_EXTERNAL_HEAT:   v = 20.0f; break;
            case ETA_AEL_PELLET_STOCK:    rec[n++] = (eta_record_t){ CONFIG_ETA_NODE, p->index, 185 }; continue;
            default: continue;
            }
            rec[n++] = (eta_record_t){ CONFIG_ETA_NODE, p->index, (int16_t)lroundf(v * 10.0f) };
        }
        s_last_data_us = esp_timer_get_time();
        set_alive(true);
        if (s_cb && s_cb->on_records) s_cb->on_records(rec, n);
        t += 0.3f;
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

esp_err_t eta_link_start(const eta_link_cb_t *cb)
{
    s_cb = cb;
    ESP_LOGW(TAG, "SIMULATION MODE: no boiler connected, values are fake");
    xTaskCreate(sim_task, "eta_sim", 4096, NULL, 4, NULL);
    return ESP_OK;
}

#else
/* ---------------- real link over UART ---------------- */

bool eta_link_send_command(uint8_t ih_bit)
{
#if !CONFIG_ETA_CONTROL_ENABLED
    ESP_LOGW(TAG, "control disabled in config, ignoring IH 0x%02x", ih_bit);
    return false;
#else
    uint8_t buf[16];
    size_t n = eta_encode_command(ih_bit, buf, sizeof buf);
    if (!n) return false;
    int w = uart_write_bytes(ETA_UART_NUM, buf, n);
    ESP_LOGI(TAG, "sent IH 0x%02x (%d bytes)", ih_bit, w);
    return w == (int)n;
#endif
}

static void send_poll(void)
{
    eta_poll_item_t items[ETA_MAX_POLL_ITEMS];
    size_t n = 0;
    for (size_t i = 0; i < eta_points_count && n < ETA_MAX_POLL_ITEMS; i++)
        items[n++] = (eta_poll_item_t){ CONFIG_ETA_NODE, eta_points[i].index };
    uint8_t buf[5 + 1 + 3 * ETA_MAX_POLL_ITEMS + 1];
    size_t len = eta_encode_poll(CONFIG_ETA_REFRESH_S, items, n, buf, sizeof buf);
    if (len) {
        uart_write_bytes(ETA_UART_NUM, buf, len);
        ESP_LOGI(TAG, "sent MC poll: %u items, refresh %d s, rx bytes so far: %lu", (unsigned)n, CONFIG_ETA_REFRESH_S,
                 (unsigned long)s_rx_total);
    }
}

static void link_task(void *arg)
{
    eta_parser_t parser;
    eta_parser_init(&parser);
    int64_t last_poll = -1;
    uint32_t bad_frames = 0;
    uint8_t rx[128];

    for (;;) {
        int64_t now = esp_timer_get_time();
        /* Poll quickly until the boiler answers, then only as keep-alive (it may have rebooted). */
        int64_t period = s_alive ? 5LL * 60 * 1000000 : 10LL * 1000000;
        if (last_poll < 0 || now - last_poll >= period) {
            send_poll();
            last_poll = now;
        }

        int got = uart_read_bytes(ETA_UART_NUM, rx, sizeof rx, pdMS_TO_TICKS(200));
        if (got > 0) {
            /* Diagnostics: hex dump of the first bytes ever received (shows wiring/baud problems). */
            if (s_rx_total < 64) {
                int show = got < (int)(64 - s_rx_total) ? got : (int)(64 - s_rx_total);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx, show, ESP_LOG_INFO);
            }
            s_rx_total += (uint32_t)got;
        }
        for (int i = 0; i < got; i++) {
            eta_parse_result_t r = eta_parser_feed(&parser, rx[i]);
            if (r == ETA_PARSE_FRAME) {
                const eta_frame_t *f = &parser.frame;
                if (f->svc[0] == 'M' && f->svc[1] == 'D') {
                    eta_record_t rec[ETA_MAX_POLL_ITEMS];
                    int n = eta_decode_md(f, rec, ETA_MAX_POLL_ITEMS);
                    if (n > 0) {
                        s_last_data_us = esp_timer_get_time();
                        set_alive(true);
                        if (s_cb && s_cb->on_records) s_cb->on_records(rec, n);
                    }
                } else if (f->svc[0] == 'I' && f->svc[1] == 'M') {
                    char text[96];
                    eta_decode_error(f, text, sizeof text);
                    ESP_LOGW(TAG, "boiler error: %s", text);
                    if (s_cb && s_cb->on_error) s_cb->on_error(text);
                } else {
                    ESP_LOGD(TAG, "ignored frame '%c%c' len %u", f->svc[0], f->svc[1], f->len);
                }
            } else if (r != ETA_PARSE_NONE) {
                bad_frames++;
                ESP_LOGW(TAG, "bad frame (%s), total %lu", r == ETA_PARSE_BAD_CHECKSUM ? "checksum" : "stop byte",
                         (unsigned long)bad_frames);
            }
        }
        check_timeout();
    }
}

esp_err_t eta_link_start(const eta_link_cb_t *cb)
{
    s_cb = cb;
    const uart_config_t cfg = {
        .baud_rate = 19200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(ETA_UART_NUM, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ETA_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(ETA_UART_NUM, CONFIG_ETA_UART_TX_GPIO, CONFIG_ETA_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART%d 19200 8N1, TX=GPIO%d RX=GPIO%d, node 0x%02x", CONFIG_ETA_UART_NUM,
             CONFIG_ETA_UART_TX_GPIO, CONFIG_ETA_UART_RX_GPIO, CONFIG_ETA_NODE);
    xTaskCreate(link_task, "eta_link", 4096, NULL, 4, NULL);
    return ESP_OK;
}
#endif
