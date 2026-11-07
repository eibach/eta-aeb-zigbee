#include "eta_points.h"

#include "eta_proto.h"

/* Index values: ETA "Protokoll uC-PC Auszug", column AEL (pellet boilers up to 30 kW). */
const eta_point_t eta_points[] = {
    { ETA_AEL_BOILER,          10, ETA_KIND_TEMP,  "boiler" },
    { ETA_AEL_FLUE_GAS,        11, ETA_KIND_TEMP,  "flue_gas" },
    { ETA_AEL_BUFFER_TOP,      12, ETA_KIND_TEMP,  "buffer_top" },
    { ETA_AEL_BUFFER_BOTTOM,   13, ETA_KIND_TEMP,  "buffer_bottom" },
    { ETA_AEL_DHW_TANK,        14, ETA_KIND_TEMP,  "dhw_tank" },
    { ETA_AEL_DHW_TANK_BOTTOM, 15, ETA_KIND_TEMP,  "dhw_tank_bottom" },
    { ETA_AEL_OUTSIDE,         16, ETA_KIND_TEMP,  "outside" },
    { ETA_AEL_FLOW_DIRECT,     17, ETA_KIND_TEMP,  "flow_direct" },
    { ETA_AEL_FLOW_MIXER0,     18, ETA_KIND_TEMP,  "flow_mixer0" },
    { ETA_AEL_EXTERNAL_HEAT,   19, ETA_KIND_TEMP,  "external_heat" },
    { ETA_AEL_PELLET_STOCK,    ETA_EP_STOCK, ETA_KIND_STOCK, "pellet_stock" },
};
const size_t eta_points_count = sizeof eta_points / sizeof eta_points[0];

const eta_point_t *eta_point_by_index(uint16_t index)
{
    for (size_t i = 0; i < eta_points_count; i++)
        if (eta_points[i].index == index) return &eta_points[i];
    return NULL;
}

int16_t eta_temp_to_zcl(int16_t raw)
{
    /* -50 .. 400 degC is plausible for this plant (flue gas can exceed 200). */
    if (raw < -500 || raw > 4000) return ETA_ZCL_TEMP_INVALID;
    return (int16_t)(raw * 10);
}

float eta_stock_to_tons(int16_t raw)
{
    return (float)raw / 100.0f;
}

uint8_t eta_mode_to_ih(uint16_t state)
{
    switch (state) {
    case ETA_MODE_RESET: return ETA_CMD_RESET;
    case ETA_MODE_AUTO:  return ETA_CMD_AUTO;
    case ETA_MODE_DAY:   return ETA_CMD_DAY;
    case ETA_MODE_NIGHT: return ETA_CMD_NIGHT;
    default:             return 0;
    }
}

uint8_t eta_boiler_cmd_to_ih(uint16_t state)
{
    switch (state) {
    case ETA_BCMD_ON:         return ETA_CMD_BOILER_ON;
    case ETA_BCMD_OFF:        return ETA_CMD_BOILER_OFF;
    case ETA_BCMD_CHARGE_DHW: return ETA_CMD_CHARGE_DHW;
    default:                  return 0; /* idle: nothing to send */
    }
}
