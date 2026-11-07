#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "eta_proto.h"
#include "eta_points.h"

static eta_parse_result_t feed_all(eta_parser_t *p, const uint8_t *b, size_t n)
{
    eta_parse_result_t r = ETA_PARSE_NONE, last = ETA_PARSE_NONE;
    for (size_t i = 0; i < n; i++) {
        r = eta_parser_feed(p, b[i]);
        if (r != ETA_PARSE_NONE) last = r;
    }
    return last;
}

static void test_encode_poll(void)
{
    uint8_t out[80];
    eta_poll_item_t it[2] = { { 0x10, 20 }, { 0x10, 134 } };
    size_t n = eta_encode_poll(10, it, 2, out, sizeof out);
    /* payload: 0A | 10 00 14 | 10 00 86 ; csum = 0x0A+0x10+0x14+0x10+0x86 = 0xC4 */
    const uint8_t exp[] = { 0x7B, 'M', 'C', 7, 0xC4, 0x0A, 0x10, 0x00, 0x14, 0x10, 0x00, 0x86, 0x7D };
    assert(n == sizeof exp);
    assert(memcmp(out, exp, n) == 0);
    assert(eta_encode_poll(10, it, 0, out, sizeof out) == 0);
    assert(eta_encode_poll(10, it, 2, out, 5) == 0); /* buffer too small */
}

static void test_encode_stop_and_command(void)
{
    uint8_t out[16];
    const uint8_t stop[] = { 0x7B, 'M', 'E', 0, 0, 0x7D };
    assert(eta_encode_stop(out, sizeof out) == sizeof stop);
    assert(memcmp(out, stop, sizeof stop) == 0);
    const uint8_t ih[] = { 0x7B, 'I', 'H', 2, 0x10, 0x10, 0x00, 0x7D };
    assert(eta_encode_command(ETA_CMD_BOILER_ON, out, sizeof out) == sizeof ih);
    assert(memcmp(out, ih, sizeof ih) == 0);
}

static void test_md_roundtrip_negative(void)
{
    /* two records: boiler 20 = 65.3 C (653 = 0x028D), outside 22 = -4.5 C (-45 = 0xFFD3) */
    uint8_t pl[] = { 0x10, 0x00, 0x14, 0x02, 0x8D, 0x10, 0x00, 0x16, 0xFF, 0xD3 };
    uint8_t fr[32];
    size_t n = eta_encode_frame("MD", pl, sizeof pl, fr, sizeof fr);
    eta_parser_t p;
    eta_parser_init(&p);
    assert(feed_all(&p, fr, n) == ETA_PARSE_FRAME);
    eta_record_t rec[4];
    int c = eta_decode_md(&p.frame, rec, 4);
    assert(c == 2);
    assert(rec[0].node == 0x10 && rec[0].index == 20 && rec[0].value == 653);
    assert(rec[1].index == 22 && rec[1].value == -45);
}

static void test_garbage_and_resync(void)
{
    uint8_t fr[32];
    uint8_t pl[] = { 0x10, 0x00, 0x56, 0x00, 0x7B }; /* payload contains '{' */
    size_t n = eta_encode_frame("MD", pl, sizeof pl, fr, sizeof fr);
    uint8_t stream[64];
    size_t k = 0;
    stream[k++] = 0xFF; stream[k++] = 0x00; stream[k++] = 0x55; /* noise */
    memcpy(&stream[k], fr, n); k += n;
    eta_parser_t p;
    eta_parser_init(&p);
    assert(feed_all(&p, stream, k) == ETA_PARSE_FRAME);
    assert(p.frame.len == 5 && p.frame.payload[4] == 0x7B);
}

static void test_bad_checksum_then_good(void)
{
    uint8_t pl[] = { 0x10, 0x00, 0x14, 0x02, 0x8D };
    uint8_t bad[16], good[16];
    size_t n = eta_encode_frame("MD", pl, sizeof pl, bad, sizeof bad);
    memcpy(good, bad, n);
    bad[4] ^= 0x01;
    eta_parser_t p;
    eta_parser_init(&p);
    assert(feed_all(&p, bad, n) == ETA_PARSE_BAD_CHECKSUM);
    assert(feed_all(&p, good, n) == ETA_PARSE_FRAME);
}

static void test_bad_stop(void)
{
    uint8_t pl[] = { 1, 2 };
    uint8_t fr[16];
    size_t n = eta_encode_frame("MD", pl, sizeof pl, fr, sizeof fr);
    fr[n - 1] = 0x00;
    eta_parser_t p;
    eta_parser_init(&p);
    assert(feed_all(&p, fr, n) == ETA_PARSE_BAD_STOP);
}

static void test_error_frame(void)
{
    const char *txt = "Zuendung Fehler";
    uint8_t fr[64];
    size_t n = eta_encode_frame("IM", (const uint8_t *)txt, (uint8_t)strlen(txt), fr, sizeof fr);
    eta_parser_t p;
    eta_parser_init(&p);
    assert(feed_all(&p, fr, n) == ETA_PARSE_FRAME);
    char s[8];
    assert(eta_decode_error(&p.frame, s, sizeof s) == 7); /* truncated */
    assert(strcmp(s, "Zuendun") == 0);
}

static void test_md_rejects_bad_length(void)
{
    eta_frame_t f = { { 'M', 'D' }, 4, { 0 } };
    eta_record_t r[2];
    assert(eta_decode_md(&f, r, 2) == -1);
    f.svc[1] = 'X'; f.len = 5;
    assert(eta_decode_md(&f, r, 2) == -1);
}

static void test_points(void)
{
    /* every endpoint unique; at most 20 poll items (protocol limit) */
    assert(eta_points_count <= ETA_MAX_POLL_ITEMS);
    for (size_t i = 0; i < eta_points_count; i++)
        for (size_t j = i + 1; j < eta_points_count; j++) {
            assert(eta_points[i].ep != eta_points[j].ep);
            assert(eta_points[i].index != eta_points[j].index);
        }
    assert(eta_point_by_index(20)->ep == 10);
    assert(eta_point_by_index(86)->kind == ETA_KIND_STOCK);
    assert(eta_point_by_index(9999) == NULL);
    /* endpoints must not collide with the fixed control endpoints */
    for (size_t i = 0; i < eta_points_count; i++) {
        assert(eta_points[i].ep != ETA_EP_MODE && eta_points[i].ep != ETA_EP_BOILER_CMD);
        assert(eta_points[i].ep != ETA_EP_FAULT);
    }
}

static void test_scaling(void)
{
    assert(eta_temp_to_zcl(653) == 6530);   /* 65.3 C -> 6530 */
    assert(eta_temp_to_zcl(-45) == -450);   /* -4.5 C */
    assert(eta_temp_to_zcl(0) == 0);
    assert(eta_temp_to_zcl(3000) == 30000); /* 300 C flue gas */
    assert(eta_temp_to_zcl(4001) == ETA_ZCL_TEMP_INVALID);
    assert(eta_temp_to_zcl(32767) == ETA_ZCL_TEMP_INVALID);
    assert(eta_temp_to_zcl(-32768) == ETA_ZCL_TEMP_INVALID);
    assert(eta_stock_to_tons(250) == 2.5f);
    assert(eta_stock_to_tons(0) == 0.0f);
    assert(eta_stock_to_tons(-99) < -0.98f && eta_stock_to_tons(-99) > -1.0f); /* real boiler: counter below zero */
    assert(eta_stock_to_tons(-32768) > ETA_STOCK_UNKNOWN); /* sentinel never collides with a real value */ /* observed on the real boiler: not available */
}

static void test_commands(void)
{
    assert(eta_mode_to_ih(ETA_MODE_RESET) == 0x01);
    assert(eta_mode_to_ih(ETA_MODE_AUTO) == 0x02);
    assert(eta_mode_to_ih(ETA_MODE_DAY) == 0x04);
    assert(eta_mode_to_ih(ETA_MODE_NIGHT) == 0x08);
    assert(eta_mode_to_ih(0) == 0 && eta_mode_to_ih(5) == 0);
    assert(eta_boiler_cmd_to_ih(ETA_BCMD_IDLE) == 0);
    assert(eta_boiler_cmd_to_ih(ETA_BCMD_ON) == 0x10);
    assert(eta_boiler_cmd_to_ih(ETA_BCMD_OFF) == 0x20);
    assert(eta_boiler_cmd_to_ih(ETA_BCMD_CHARGE_DHW) == 0x40);
    assert(eta_boiler_cmd_to_ih(99) == 0);
}

/* Frames captured on a real ETA PE-25 (2026-11-06). */
static const uint8_t k_real_poll[] = {
    0x7b, 0x4d, 0x43, 0x22, 0x1e, 0x1e, 0x10, 0x00, 0x14, 0x10, 0x00, 0x13, 0x10, 0x00, 0x4d, 0x10, 0x00, 0x4e,
    0x10, 0x00, 0x15, 0x10, 0x00, 0x86, 0x10, 0x00, 0x16, 0x10, 0x00, 0x17, 0x10, 0x00, 0x18, 0x10, 0x00, 0x58,
    0x10, 0x00, 0x56, 0x7d };
static const uint8_t k_real_reply[] = {
    0x7b, 0x4d, 0x44, 0x37, 0x0e, 0x10, 0x00, 0x13, 0x03, 0x59, 0x10, 0x00, 0x14, 0x02, 0x62, 0x10, 0x00, 0x15,
    0x00, 0x00, 0x10, 0x00, 0x16, 0x00, 0x01, 0x10, 0x00, 0x17, 0x02, 0x02, 0x10, 0x00, 0x18, 0x01, 0xac, 0x10,
    0x00, 0x4d, 0x00, 0x00, 0x10, 0x00, 0x4e, 0x00, 0x00, 0x10, 0x00, 0x56, 0xff, 0x9d, 0x10, 0x00, 0x58, 0x00,
    0x00, 0x10, 0x00, 0x86, 0x00, 0x00, 0x7d };

static void test_real_capture(void)
{
    /* The poll we build for the point table is byte-identical to the one the boiler answered. */
    eta_poll_item_t items[ETA_MAX_POLL_ITEMS];
    size_t n = 0;
    for (size_t i = 0; i < eta_points_count; i++) items[n++] = (eta_poll_item_t){ ETA_NODE_PELLET_30KW, eta_points[i].index };
    uint8_t out[64];
    size_t len = eta_encode_poll(30, items, n, out, sizeof out);
    assert(len == sizeof k_real_poll && memcmp(out, k_real_poll, len) == 0);

    /* The boiler's reply decodes to the values shown on its display that day. Replies come sorted by index. */
    eta_parser_t p;
    eta_parser_init(&p);
    assert(feed_all(&p, k_real_reply, sizeof k_real_reply) == ETA_PARSE_FRAME);
    eta_record_t rec[ETA_MAX_POLL_ITEMS];
    int c = eta_decode_md(&p.frame, rec, ETA_MAX_POLL_ITEMS);
    assert(c == 11);
    for (int i = 1; i < c; i++) assert(rec[i].index > rec[i - 1].index);
    for (int i = 0; i < c; i++) {
        assert(rec[i].node == ETA_NODE_PELLET_30KW);
        const eta_point_t *pt = eta_point_by_index(rec[i].index);
        assert(pt);
        switch (rec[i].index) {
        case ETA_AEL_FLUE_GAS:      assert(rec[i].value == 857 && eta_temp_to_zcl(rec[i].value) == 8570); break;
        case ETA_AEL_BOILER:        assert(rec[i].value == 610); break;
        case ETA_AEL_OUTSIDE:       assert(rec[i].value == 1); break;
        case ETA_AEL_FLOW_DIRECT:   assert(rec[i].value == 514); break;
        case ETA_AEL_FLOW_MIXER0:   assert(rec[i].value == 428); break;
        case ETA_AEL_PELLET_STOCK:  assert(rec[i].value == -99 && eta_stock_to_tons(rec[i].value) < -0.98f); break;
        default:                    assert(rec[i].value == 0); break; /* sensors not installed */
        }
    }
}

int main(void)
{
    test_encode_poll();
    test_encode_stop_and_command();
    test_md_roundtrip_negative();
    test_garbage_and_resync();
    test_bad_checksum_then_good();
    test_bad_stop();
    test_error_frame();
    test_md_rejects_bad_length();
    test_points();
    test_scaling();
    test_commands();
    test_real_capture();
    puts("all eta_proto tests passed");
    return 0;
}
