/*
 * ETA PE-25 Zigbee bridge: ESP32-C6 Zigbee router + RS-232 link to the boiler.
 *
 * Endpoints (see components/eta_proto/include/eta_points.h):
 *   10..19  temperature sensors (boiler, flue gas, buffer, hot water, outside, flows, external heat)
 *   20      analog input    : pellet stock [t]
 *   21      multistate out. : heating mode (reset/auto/day/night)      -> IH command
 *   22      multistate out. : boiler command (idle/on/off/charge DHW)  -> IH command, resets to idle
 *   23      binary input    : boiler reported an error
 *
 * Joins an existing network only (never forms one). Pair via Zigbee2MQTT permit-join.
 */
#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "esp_zigbee.h"
#include "ezbee/zha.h"

#include "eta_link.h"
#include "eta_points.h"
#include "sdkconfig.h"
#include "status_led.h"

static const char *TAG = "ETA_ZB";

/* Zigbee channels 11-26. The primary scan tries your network's channel first, then everything
 * else is scanned (secondary mask), so this works on any channel. */
#define ETA_CHANNEL_MASK 0x07FFF800U
#define ETA_PRIMARY_MASK (1U << CONFIG_ETA_ZB_PRIMARY_CHANNEL)

#define ETA_MANUFACTURER "\x03""ETA"
#define ETA_MODEL        "\x07""PE25-ZB"

#define STEERING_RETRY_MS     5000
#define FAULT_HOLD_MS         (30 * 60 * 1000)
#define BOILER_CMD_RESET_MS   2000

static esp_timer_handle_t s_retry_timer, s_cmd_reset_timer, s_fault_timer;
static volatile bool s_zb_ready;
static volatile bool s_fault;

/* ------------------------------------------------------------------ LED */
static void refresh_led(void)
{
    if (!s_zb_ready) status_led_set(LED_NOT_JOINED);
    else if (s_fault) status_led_set(LED_FAULT);
    else status_led_set(eta_link_alive() ? LED_OK : LED_NO_BOILER);
}

/* ------------------------------------------------------- attribute updates */
/* Callers must NOT hold the Zigbee lock; these take it themselves. */
static void set_temp(uint8_t ep, int16_t zcl_value)
{
    ezb_zcl_set_attr_value(ep, EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT, EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID, EZB_ZCL_STD_MANUF_CODE,
                           &zcl_value, false);
}

static void set_stock(float tons)
{
    ezb_zcl_set_attr_value(ETA_EP_STOCK, EZB_ZCL_CLUSTER_ID_ANALOG_INPUT, EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID, EZB_ZCL_STD_MANUF_CODE, &tons, false);
}

static void set_multistate(uint8_t ep, uint16_t state)
{
    ezb_zcl_set_attr_value(ep, EZB_ZCL_CLUSTER_ID_MULTISTATE_OUTPUT, EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_MULTISTATE_OUTPUT_PRESENT_VALUE_ID, EZB_ZCL_STD_MANUF_CODE, &state, false);
}

static void set_fault(bool fault)
{
    s_fault = fault;
    ezb_zcl_set_attr_value(ETA_EP_FAULT, EZB_ZCL_CLUSTER_ID_BINARY_INPUT, EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID, EZB_ZCL_STD_MANUF_CODE, &fault, false);
}

static void on_records(const eta_record_t *rec, int n)
{
    if (!s_zb_ready) return;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    for (int i = 0; i < n; i++) {
        if (rec[i].node != CONFIG_ETA_NODE) {
            ESP_LOGW(TAG, "record from unexpected node 0x%02x (configured 0x%02x)", rec[i].node, CONFIG_ETA_NODE);
            continue;
        }
        const eta_point_t *p = eta_point_by_index(rec[i].index);
        if (!p) continue;
        if (p->kind == ETA_KIND_TEMP) {
            set_temp(p->ep, eta_temp_to_zcl(rec[i].value));
        } else {
            set_stock(eta_stock_to_tons(rec[i].value));
        }
        ESP_LOGD(TAG, "%s = %d", p->name, rec[i].value);
    }
    esp_zigbee_lock_release();
    refresh_led();
}

static void on_link(bool alive)
{
    if (!alive && s_zb_ready) {
        /* Boiler stopped answering: show "unknown" instead of stale values. */
        esp_zigbee_lock_acquire(portMAX_DELAY);
        for (size_t i = 0; i < eta_points_count; i++) {
            if (eta_points[i].kind == ETA_KIND_TEMP) set_temp(eta_points[i].ep, ETA_ZCL_TEMP_INVALID);
            else set_stock(ETA_STOCK_UNKNOWN);
        }
        esp_zigbee_lock_release();
    }
    refresh_led();
}

static void fault_clear_cb(void *arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    set_fault(false);
    esp_zigbee_lock_release();
    refresh_led();
}

static void on_error(const char *text)
{
    if (!s_zb_ready) return;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    set_fault(true);
    esp_zigbee_lock_release();
    /* The protocol has no "error cleared" message: hold the flag for a while, refresh on repeats. */
    esp_timer_stop(s_fault_timer);
    esp_timer_start_once(s_fault_timer, FAULT_HOLD_MS * 1000ULL);
    refresh_led();
}

static void cmd_reset_cb(void *arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    set_multistate(ETA_EP_BOILER_CMD, ETA_BCMD_IDLE);
    esp_zigbee_lock_release();
}

/* ----------------------------------------------------- Zigbee commissioning */
static void steering_retry_cb(void *arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    (void)ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
    esp_zigbee_lock_release();
}

static bool app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t type = ezb_app_signal_get_type(app_signal);

    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status != EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "%s failed, status 0x%02x", ezb_app_signal_to_string(type), status);
            break;
        }
        if (ezb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "Factory new: searching for a network to join (enable permit join)");
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGI(TAG, "Rejoined network: PAN 0x%04hx, channel %d, short 0x%04hx", ezb_nwk_get_panid(),
                     ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
            s_zb_ready = true;
            refresh_led();
        }
    } break;

    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Joined network: PAN 0x%04hx, channel %d, short 0x%04hx", ezb_nwk_get_panid(),
                     ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
            s_zb_ready = true;
            refresh_led();
        } else {
            ESP_LOGW(TAG, "No network joined (status 0x%02x), retrying in %d ms", status, STEERING_RETRY_MS);
            (void)esp_timer_stop(s_retry_timer);
            esp_timer_start_once(s_retry_timer, STEERING_RETRY_MS * 1000ULL);
        }
    } break;

    default:
        ESP_LOGD(TAG, "Zigbee signal: %s (0x%02x)", ezb_app_signal_to_string(type), type);
        break;
    }
    return true;
}

/* Writes from the coordinator (Zigbee2MQTT / HA). Runs in the Zigbee task: no lock needed. */
static void zcl_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    if (callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID) return;

    ezb_zcl_set_attr_value_message_t *m = message;
    if (m->info.cluster_id != EZB_ZCL_CLUSTER_ID_MULTISTATE_OUTPUT ||
        m->in.attribute.id != EZB_ZCL_ATTR_MULTISTATE_OUTPUT_PRESENT_VALUE_ID || !m->in.attribute.data.value) {
        return;
    }
    uint16_t state = *(uint16_t *)m->in.attribute.data.value;

    if (m->info.dst_ep == ETA_EP_MODE) {
        uint8_t ih = eta_mode_to_ih(state);
        if (!ih) { m->out.result = EZB_ZCL_STATUS_INVALID_VALUE; return; }
        ESP_LOGI(TAG, "heating mode -> %u", state);
        if (!eta_link_send_command(ih)) m->out.result = EZB_ZCL_STATUS_FAIL;
    } else if (m->info.dst_ep == ETA_EP_BOILER_CMD) {
        if (state < 1 || state > ETA_BCMD_COUNT) { m->out.result = EZB_ZCL_STATUS_INVALID_VALUE; return; }
        uint8_t ih = eta_boiler_cmd_to_ih(state);
        if (!ih) return; /* idle: just the reset value */
        ESP_LOGW(TAG, "boiler command -> %u (IH 0x%02x)", state, ih);
        if (!eta_link_send_command(ih)) { m->out.result = EZB_ZCL_STATUS_FAIL; return; }
        esp_timer_stop(s_cmd_reset_timer);
        esp_timer_start_once(s_cmd_reset_timer, BOILER_CMD_RESET_MS * 1000ULL);
    }
}

/* ------------------------------------------------------------ device setup */
static void add_ep(ezb_af_device_desc_t dev, uint8_t ep_id, uint16_t device_id, ezb_zcl_cluster_desc_t cl)
{
    ezb_af_ep_config_t cfg = {
        .ep_id = ep_id,
        .app_profile_id = EZB_AF_HA_PROFILE_ID,
        .app_device_id = device_id,
        .app_device_version = 0,
    };
    ezb_af_ep_desc_t ep = ezb_af_create_endpoint_desc(&cfg);
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep, cl));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev, ep));
}

static esp_err_t create_device(void)
{
    ezb_af_device_desc_t dev = ezb_af_create_device_desc();
    bool first = true;

    /* Temperature endpoints (standard HA temperature sensor, incl. Basic + Identify). */
    for (size_t i = 0; i < eta_points_count; i++) {
        if (eta_points[i].kind != ETA_KIND_TEMP) continue;
        ezb_zha_temperature_sensor_config_t cfg = EZB_ZHA_TEMPERATURE_SENSOR_CONFIG();
        cfg.temp_meas_cfg.min_measured_value = -5000; /* -50.00 C */
        cfg.temp_meas_cfg.max_measured_value = 30000; /* 300.00 C */
        ezb_af_ep_desc_t ep = ezb_zha_create_temperature_sensor(eta_points[i].ep, &cfg);
        if (first) {
            ezb_zcl_cluster_desc_t basic =
                ezb_af_endpoint_get_cluster_desc(ep, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
            ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                                (void *)ETA_MANUFACTURER);
            ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)ETA_MODEL);
            first = false;
        }
        ESP_RETURN_ON_ERROR(ezb_af_device_add_endpoint_desc(dev, ep), TAG, "add temp ep");
    }

    /* Pellet stock (analog input, tons). Negative = unknown. */
    ezb_zcl_analog_input_cluster_config_t ai = { .out_of_service = false, .present_value = ETA_STOCK_UNKNOWN, .status_flags = 0 };
    add_ep(dev, ETA_EP_STOCK, EZB_ZHA_SIMPLE_SENSOR_DEVICE_ID,
           ezb_zcl_analog_input_create_cluster_desc(&ai, EZB_ZCL_CLUSTER_SERVER));

    /* Heating mode and boiler command (multistate outputs, 1-based). */
    ezb_zcl_multistate_output_cluster_config_t mode = {
        .number_of_states = ETA_MODE_COUNT, .out_of_service = false, .present_value = ETA_MODE_RESET, .status_flags = 0 };
    add_ep(dev, ETA_EP_MODE, EZB_ZHA_COMBINED_INTERFACE_DEVICE_ID,
           ezb_zcl_multistate_output_create_cluster_desc(&mode, EZB_ZCL_CLUSTER_SERVER));
    ezb_zcl_multistate_output_cluster_config_t bcmd = {
        .number_of_states = ETA_BCMD_COUNT, .out_of_service = false, .present_value = ETA_BCMD_IDLE, .status_flags = 0 };
    add_ep(dev, ETA_EP_BOILER_CMD, EZB_ZHA_COMBINED_INTERFACE_DEVICE_ID,
           ezb_zcl_multistate_output_create_cluster_desc(&bcmd, EZB_ZCL_CLUSTER_SERVER));

    /* Fault flag (binary input). */
    ezb_zcl_binary_input_cluster_config_t bi = { .out_of_service = false, .present_value = false, .status_flags = 0 };
    add_ep(dev, ETA_EP_FAULT, EZB_ZHA_SIMPLE_SENSOR_DEVICE_ID,
           ezb_zcl_binary_input_create_cluster_desc(&bi, EZB_ZCL_CLUSTER_SERVER));

    ESP_RETURN_ON_ERROR(ezb_af_device_desc_register(dev), TAG, "register device");
    ezb_zcl_core_action_handler_register(zcl_action_handler);
    return ESP_OK;
}

static void zigbee_main_task(void *arg)
{
    esp_zigbee_config_t config = {
        .device_config = {
            .device_type = EZB_NWK_DEVICE_TYPE_ROUTER,
            .install_code_policy = false,
            .zczr_config = { .max_children = 10 },
        },
        .platform_config = {
            .storage_partition_name = "nvs",
            .radio_config = { .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE },
        },
    };

    ESP_ERROR_CHECK(esp_zigbee_init(&config));

    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(ETA_PRIMARY_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(ETA_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(app_signal_handler));

    ESP_ERROR_CHECK(create_device());
    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();

    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    status_led_start();

    const esp_timer_create_args_t a1 = { .callback = steering_retry_cb, .name = "steer_retry" };
    const esp_timer_create_args_t a2 = { .callback = cmd_reset_cb, .name = "cmd_reset" };
    const esp_timer_create_args_t a3 = { .callback = fault_clear_cb, .name = "fault_clear" };
    ESP_ERROR_CHECK(esp_timer_create(&a1, &s_retry_timer));
    ESP_ERROR_CHECK(esp_timer_create(&a2, &s_cmd_reset_timer));
    ESP_ERROR_CHECK(esp_timer_create(&a3, &s_fault_timer));

    ESP_LOGI(TAG, "Starting ETA Zigbee router");
    xTaskCreate(zigbee_main_task, "zigbee_main", 6144, NULL, 5, NULL);

    static const eta_link_cb_t cb = { .on_records = on_records, .on_link = on_link, .on_error = on_error };
    ESP_ERROR_CHECK(eta_link_start(&cb));
}
