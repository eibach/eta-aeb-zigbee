#include "eta_proto.h"

#include <string.h>

enum { S_START, S_SVC1, S_SVC2, S_LEN, S_CSUM, S_DATA, S_STOP };

static uint8_t checksum(const uint8_t *d, size_t n)
{
    uint8_t s = 0;
    while (n--) s = (uint8_t)(s + *d++);
    return s;
}

size_t eta_encode_frame(const char svc[2], const uint8_t *payload, uint8_t len,
                        uint8_t *out, size_t cap)
{
    size_t total = (size_t)ETA_HEADER_LEN + len + 1;
    if (!svc || !out || (len && !payload) || cap < total) return 0;
    out[0] = ETA_START_BYTE;
    out[1] = (uint8_t)svc[0];
    out[2] = (uint8_t)svc[1];
    out[3] = len;
    out[4] = checksum(payload, len);
    if (len) memcpy(&out[ETA_HEADER_LEN], payload, len);
    out[ETA_HEADER_LEN + len] = ETA_STOP_BYTE;
    return total;
}

size_t eta_encode_poll(uint8_t refresh_s, const eta_poll_item_t *items, size_t n,
                       uint8_t *out, size_t cap)
{
    uint8_t pl[1 + 3 * ETA_MAX_POLL_ITEMS];
    if (n == 0 || n > ETA_MAX_POLL_ITEMS || !items) return 0;
    pl[0] = refresh_s;
    for (size_t i = 0; i < n; i++) {
        pl[1 + 3 * i] = items[i].node;
        pl[2 + 3 * i] = (uint8_t)(items[i].index >> 8); /* high byte first */
        pl[3 + 3 * i] = (uint8_t)(items[i].index & 0xFF);
    }
    return eta_encode_frame("MC", pl, (uint8_t)(1 + 3 * n), out, cap);
}

size_t eta_encode_stop(uint8_t *out, size_t cap)
{
    return eta_encode_frame("ME", NULL, 0, out, cap);
}

size_t eta_encode_command(uint8_t cmd, uint8_t *out, size_t cap)
{
    uint8_t pl[2] = { cmd, 0x00 };
    return eta_encode_frame("IH", pl, 2, out, cap);
}

int eta_decode_md(const eta_frame_t *f, eta_record_t *out, size_t max)
{
    if (!f || !out || f->svc[0] != 'M' || f->svc[1] != 'D') return -1;
    if (f->len % 5 != 0) return -1;
    size_t n = f->len / 5;
    if (n > max) n = max;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *r = &f->payload[5 * i];
        out[i].node = r[0];
        out[i].index = (uint16_t)((r[1] << 8) | r[2]);
        out[i].value = (int16_t)(uint16_t)((r[3] << 8) | r[4]);
    }
    return (int)n;
}

int eta_decode_error(const eta_frame_t *f, char *out, size_t cap)
{
    if (!f || !out || cap == 0 || f->svc[0] != 'I' || f->svc[1] != 'M') return -1;
    size_t n = f->len < cap - 1 ? f->len : cap - 1;
    memcpy(out, f->payload, n);
    out[n] = '\0';
    return (int)n;
}

void eta_parser_init(eta_parser_t *p)
{
    memset(p, 0, sizeof *p);
    p->state = S_START;
}

eta_parse_result_t eta_parser_feed(eta_parser_t *p, uint8_t b)
{
    switch (p->state) {
    case S_START:
        if (b == ETA_START_BYTE) p->state = S_SVC1;
        break;
    case S_SVC1:
        p->frame.svc[0] = (char)b;
        p->state = S_SVC2;
        break;
    case S_SVC2:
        p->frame.svc[1] = (char)b;
        p->state = S_LEN;
        break;
    case S_LEN:
        p->frame.len = b;
        p->pos = 0;
        p->csum_calc = 0;
        p->state = S_CSUM;
        break;
    case S_CSUM:
        p->csum_rx = b;
        p->state = p->frame.len ? S_DATA : S_STOP;
        break;
    case S_DATA:
        p->frame.payload[p->pos++] = b;
        p->csum_calc = (uint8_t)(p->csum_calc + b);
        if (p->pos == p->frame.len) p->state = S_STOP;
        break;
    case S_STOP:
        p->state = S_START;
        if (b != ETA_STOP_BYTE) return ETA_PARSE_BAD_STOP;
        if (p->csum_calc != p->csum_rx) return ETA_PARSE_BAD_CHECKSUM;
        return ETA_PARSE_FRAME;
    }
    return ETA_PARSE_NONE;
}
