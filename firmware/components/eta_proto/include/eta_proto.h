/*
 * ETA AEB controller serial protocol (uC <-> PC over RS232), 19200 8N1.
 * Source: "Protokoll uC-PC Auszug" (ETA, 19.01.2007).
 *
 * Frame:  '{' | svc[2] | len | checksum | payload[len] | '}'
 *         checksum = sum(payload) mod 256
 *
 * Portable C99, no dynamic allocation, no platform dependencies.
 */
#ifndef ETA_PROTO_H
#define ETA_PROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETA_START_BYTE 0x7B /* '{' */
#define ETA_STOP_BYTE  0x7D /* '}' */
#define ETA_HEADER_LEN 5    /* start + 2 svc + len + checksum */
#define ETA_MAX_PAYLOAD 255
#define ETA_MAX_POLL_ITEMS 20

/* Node numbers (base; add the DIP switch setting, normally 0). */
#define ETA_NODE_LOGWOOD     0x08
#define ETA_NODE_PELLET_30KW 0x10 /* AEL: pellet boiler up to 30 kW (PE-25) */
#define ETA_NODE_PELLET_35KW 0x18
#define ETA_NODE_EXTENSION   0x20

/* Monitor list indices for AEL (pellet boiler up to 30 kW). */
enum eta_ael_index {
    ETA_AEL_BOILER          = 20,
    ETA_AEL_EXTERNAL_HEAT   = 88,
    ETA_AEL_FLUE_GAS        = 19,
    ETA_AEL_BUFFER_TOP      = 77,
    ETA_AEL_BUFFER_BOTTOM   = 78,
    ETA_AEL_DHW_TANK        = 21,
    ETA_AEL_DHW_TANK_BOTTOM = 134,
    ETA_AEL_OUTSIDE         = 22,
    ETA_AEL_FLOW_DIRECT     = 23,
    ETA_AEL_ROOM_DIRECT     = 25,
    ETA_AEL_FLOW_MIXER0     = 24,
    ETA_AEL_ROOM_MIXER0     = 124,
    ETA_AEL_THERMOSTAT_HOT  = 96,
    ETA_AEL_THERMOSTAT_COLD = 97,
    ETA_AEL_PELLET_STOCK    = 86, /* tons * 100 */
};

/* "IH" command bits (Data[0]). */
enum eta_heating_cmd {
    ETA_CMD_RESET    = 0x01, /* use the physical switch */
    ETA_CMD_AUTO     = 0x02,
    ETA_CMD_DAY      = 0x04,
    ETA_CMD_NIGHT    = 0x08,
    ETA_CMD_BOILER_ON  = 0x10,
    ETA_CMD_BOILER_OFF = 0x20,
    ETA_CMD_CHARGE_DHW = 0x40,
};

typedef struct {
    uint8_t node;
    uint16_t index;
} eta_poll_item_t;

typedef struct {
    uint8_t node;
    uint16_t index;
    int16_t value; /* temperatures: degC * 10; pellet stock: tons * 100 */
} eta_record_t;

typedef struct {
    char svc[2];
    uint8_t len;
    uint8_t payload[ETA_MAX_PAYLOAD];
} eta_frame_t;

/* ---- encoding: return frame length, or 0 if the buffer is too small / bad args ---- */
size_t eta_encode_frame(const char svc[2], const uint8_t *payload, uint8_t len,
                        uint8_t *out, size_t cap);
/* "MC": request data; refresh_s = seconds between "MD" frames; max 20 items. */
size_t eta_encode_poll(uint8_t refresh_s, const eta_poll_item_t *items, size_t n,
                       uint8_t *out, size_t cap);
/* "ME": stop data recording. */
size_t eta_encode_stop(uint8_t *out, size_t cap);
/* "IH": heating/boiler command, see enum eta_heating_cmd (one bit at a time). */
size_t eta_encode_command(uint8_t cmd, uint8_t *out, size_t cap);

/* ---- decoding ---- */
/* Decode an "MD" frame into records. Returns count, or -1 if not a valid MD frame. */
int eta_decode_md(const eta_frame_t *f, eta_record_t *out, size_t max);
/* Copy an "IM" error text (NUL-terminated, truncated) into out. Returns length, or -1. */
int eta_decode_error(const eta_frame_t *f, char *out, size_t cap);

/* ---- streaming parser: feed received bytes one at a time ---- */
typedef enum {
    ETA_PARSE_NONE = 0,   /* need more bytes */
    ETA_PARSE_FRAME,      /* a valid frame is available in parser->frame */
    ETA_PARSE_BAD_CHECKSUM,
    ETA_PARSE_BAD_STOP,
} eta_parse_result_t;

typedef struct {
    uint8_t state;
    uint8_t pos;
    uint8_t csum_rx;
    uint8_t csum_calc;
    eta_frame_t frame;
} eta_parser_t;

void eta_parser_init(eta_parser_t *p);
eta_parse_result_t eta_parser_feed(eta_parser_t *p, uint8_t byte);

#ifdef __cplusplus
}
#endif
#endif /* ETA_PROTO_H */
