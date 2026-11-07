#pragma once

typedef enum {
    LED_NOT_JOINED, /* red, blinking       : no Zigbee network yet            */
    LED_NO_BOILER,  /* yellow, steady      : joined, boiler not answering     */
    LED_OK,         /* green, steady (dim) : joined and boiler data flowing   */
    LED_FAULT,      /* red, steady         : boiler reported an error         */
} status_led_state_t;

void status_led_start(void);
void status_led_set(status_led_state_t s);
