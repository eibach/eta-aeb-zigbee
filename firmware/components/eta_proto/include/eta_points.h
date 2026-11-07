/*
 * Which boiler values are exposed over Zigbee, and how they are converted.
 * Pure C, no platform dependencies (host-testable).
 */
#ifndef ETA_POINTS_H
#define ETA_POINTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ETA_KIND_TEMP,  /* degC*10 from the boiler -> Zigbee temperature cluster (degC*100) */
    ETA_KIND_STOCK, /* tons*100 from the boiler -> Zigbee analog input (float, tons)    */
} eta_kind_t;

typedef struct {
    uint16_t index;   /* monitor list index (AEL column of the ETA document) */
    uint8_t ep;       /* Zigbee endpoint */
    eta_kind_t kind;
    const char *name; /* for logs; Zigbee2MQTT names come from z2m/pe25_zb.mjs */
} eta_point_t;

/* Zigbee endpoint layout. Temperatures: ETA_EP_TEMP_FIRST .. (see table). */
#define ETA_EP_TEMP_FIRST   10
#define ETA_EP_STOCK        20 /* analog input: pellet stock in tons */
#define ETA_EP_MODE         21 /* multistate output: heating mode (IH) */
#define ETA_EP_BOILER_CMD   22 /* multistate output: boiler command (IH), self-resetting */
#define ETA_EP_FAULT        23 /* binary input: boiler reported an error */

/* Multistate output states (ZCL multistate values are 1-based). */
enum { ETA_MODE_RESET = 1, ETA_MODE_AUTO = 2, ETA_MODE_DAY = 3, ETA_MODE_NIGHT = 4, ETA_MODE_COUNT = 4 };
enum { ETA_BCMD_IDLE = 1, ETA_BCMD_ON = 2, ETA_BCMD_OFF = 3, ETA_BCMD_CHARGE_DHW = 4, ETA_BCMD_COUNT = 4 };

#define ETA_ZCL_TEMP_INVALID ((int16_t)0x8000)
/* Pellet stock "unknown" (boiler silent). The real value is raw/100 tons with raw in
 * int16 range (-327.68..327.67) and CAN be negative (ETA tracks the stock as a counter that the
 * owner corrects after each delivery), so the sentinel lies far outside that range. NaN would not
 * be reported by Zigbee. The Zigbee2MQTT converter maps anything <= -1000 to null. */
#define ETA_STOCK_UNKNOWN (-1000.0f)

extern const eta_point_t eta_points[];
extern const size_t eta_points_count;

const eta_point_t *eta_point_by_index(uint16_t index);

/* raw = degC*10. Returns ZCL temperature (degC*100) or ETA_ZCL_TEMP_INVALID when implausible. */
int16_t eta_temp_to_zcl(int16_t raw);
/* raw = tons*100. May be negative (stock counter not corrected after a delivery). */
float eta_stock_to_tons(int16_t raw);

/* Map a multistate state to the IH command bit (Data[0]); 0 = nothing to send / invalid. */
uint8_t eta_mode_to_ih(uint16_t state);
uint8_t eta_boiler_cmd_to_ih(uint16_t state);

#ifdef __cplusplus
}
#endif
#endif
