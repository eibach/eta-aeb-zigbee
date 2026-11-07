/*
 * Diagnostic: 802.15.4 channel scanner. For each channel 11-26, sends beacon requests
 * and counts every frame heard (promiscuous), printing beacons (PAN, permit-join flag).
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_ieee802154.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SCAN";

typedef struct { uint16_t pan; uint16_t src; uint8_t permit; int8_t rssi; uint8_t pl[15]; uint8_t pllen; } beacon_t;

static volatile uint32_t s_frames;
static volatile int8_t s_max_rssi;
static volatile uint32_t s_nbeacons;
static beacon_t s_beacons[8];

void esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *info)
{
    uint8_t len = frame[0];
    s_frames++;
    if (info->rssi > s_max_rssi) s_max_rssi = info->rssi;
    if (len >= 11) {
        uint16_t fcf = frame[1] | (frame[2] << 8);
        if ((fcf & 7) == 0 && ((fcf >> 14) & 3) == 2) { /* beacon, short src addr */
            uint16_t pan = frame[4] | (frame[5] << 8);
            uint16_t src = frame[6] | (frame[7] << 8);
            uint16_t sf  = frame[8] | (frame[9] << 8);
            if (s_nbeacons < 8) {
                beacon_t *b = &s_beacons[s_nbeacons];
                *b = (beacon_t){ .pan = pan, .src = src, .permit = (sf >> 15) & 1, .rssi = info->rssi };
                int plen = len - 2 - 11; /* minus FCS and MAC header+SF+GTS+pending */
                if (plen > 15) plen = 15;
                if (plen > 0) { memcpy(b->pl, &frame[12], plen); b->pllen = plen; }
            }
            s_nbeacons++;
        }
    }
    esp_ieee802154_receive_handle_done(frame);
}

void esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack, esp_ieee802154_frame_info_t *ack_info)
{
    /* rx_when_idle returns the radio to receive; do NOT call esp_ieee802154_receive() here */
}
void esp_ieee802154_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error)
{
    /* calling esp_ieee802154_receive() from here recurses (it cancels the tx -> this callback) */
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_ieee802154_enable());
    esp_ieee802154_set_promiscuous(true);
    esp_ieee802154_set_rx_when_idle(true);
    esp_ieee802154_set_txpower(20);
    uint8_t seq = 0;

    for (int round = 1;; round++) {
        ESP_LOGI(TAG, "---- round %d ----", round);
        for (int ch = 11; ch <= 26; ch++) {
            s_frames = 0; s_nbeacons = 0; s_max_rssi = -127;
            esp_ieee802154_set_channel(ch);
            esp_ieee802154_receive();
            for (int i = 0; i < 4; i++) {
                /* len(incl FCS), FCF(cmd, dst short), seq, dst PAN, dst addr, cmd 0x07 beacon request */
                uint8_t req[] = { 10, 0x03, 0x08, seq++, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0, 0 };
                esp_ieee802154_transmit(req, false);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            uint32_t nb = s_nbeacons;
            ESP_LOGI(TAG, "ch %2d: frames=%-4lu max_rssi=%-4d beacons=%lu", ch, (unsigned long)s_frames,
                     s_frames ? s_max_rssi : 0, (unsigned long)nb);
            for (uint32_t b = 0; b < nb && b < 8; b++) {
                const beacon_t *x = &s_beacons[b];
                ESP_LOGI(TAG, "    BEACON pan=0x%04x src=0x%04x permit_join=%d rssi=%d", x->pan, x->src, x->permit, x->rssi);
                if (x->pllen >= 15) {
                    ESP_LOGI(TAG, "      proto=%d stack_profile=%d nwk_ver=%d router_cap=%d depth=%d enddev_cap=%d extpan=%02x%02x%02x%02x%02x%02x%02x%02x upd_id=%d",
                             x->pl[0], x->pl[1] & 15, x->pl[1] >> 4, (x->pl[2] >> 2) & 1, (x->pl[2] >> 3) & 15, (x->pl[2] >> 7) & 1,
                             x->pl[10], x->pl[9], x->pl[8], x->pl[7], x->pl[6], x->pl[5], x->pl[4], x->pl[3], x->pl[14]);
                }
            }
        }
    }
}
