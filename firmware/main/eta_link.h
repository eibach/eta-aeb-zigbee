#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "eta_proto.h"

typedef struct {
    /* Called from the link task with fresh values (MD frame). */
    void (*on_records)(const eta_record_t *rec, int n);
    /* Called when the boiler starts / stops answering. */
    void (*on_link)(bool alive);
    /* Boiler reported an error text (IM frame), NUL-terminated. */
    void (*on_error)(const char *text);
} eta_link_cb_t;

esp_err_t eta_link_start(const eta_link_cb_t *cb);
/* Send an "IH" command (one bit of enum eta_heating_cmd). false if disabled or not sent. */
bool eta_link_send_command(uint8_t ih_bit);
bool eta_link_alive(void);
