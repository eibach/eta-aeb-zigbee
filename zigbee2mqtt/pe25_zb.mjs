// Zigbee2MQTT external converter for the ETA PE-25 Zigbee bridge (ESP32-C6, firmware in this repo).
// Install: copy to <z2m data dir>/external_converters/pe25_zb.mjs, restart Zigbee2MQTT,
// then (re)pair the device (endpoints changed vs. the skeleton firmware).
//
// Endpoint layout must match components/eta_proto/include/eta_points.h:
//   10..19 temperatures, 20 pellet stock, 21 heating mode, 22 boiler command, 23 fault.
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

const temps = [
    ['boiler', 10, 'Boiler'],
    ['flue_gas', 11, 'Flue gas'],
    ['buffer_top', 12, 'Buffer top'],
    ['buffer_bottom', 13, 'Buffer bottom'],
    ['dhw_tank', 14, 'Hot water tank'],
    ['dhw_tank_bottom', 15, 'Hot water tank bottom'],
    ['outside', 16, 'Outside'],
    ['flow_direct', 17, 'Flow direct circuit'],
    ['flow_mixer0', 18, 'Flow mixer circuit 0'],
    ['external_heat', 19, 'External heat'],
];

const idToName = {};
for (const [name, id] of temps) idToName[id] = name;

const endpoints = {};
for (const [name, id] of temps) endpoints[name] = id;
endpoints.mode = 21;
endpoints.command = 22;

export default {
    zigbeeModel: ['PE25-ZB'],
    model: 'PE25-ZB',
    vendor: 'ETA',
    description: 'ETA PE-25 pellet boiler serial bridge (ESP32-C6, Zigbee router)',
    extend: [
        m.deviceEndpoints({endpoints}),

        m.temperature({
            endpointNames: temps.map((t) => t[0]),
            label: temps.map((t) => t[2]),
            // Without a name Home Assistant calls every sensor just "Temperature".
            homeassistant: temps.map((t) => ({name: `${t[2]} temperature`})),
            description: 'Temperature reported by the boiler controller',
            // The firmware reports 0x8000 (ZCL "invalid") while the boiler does not answer: show unknown.
            fzConvert: (model, msg, publish, options, meta) => {
                const raw = msg.data.measuredValue;
                const name = idToName[msg.endpoint.ID];
                if (raw === undefined || name === undefined) return;
                return {[`temperature_${name}`]: raw === -32768 ? null : raw / 100};
            },
        }),

        // Pellet stock in tons (analog input, endpoint 20).
        m.numeric({
            name: 'pellet_stock',
            label: 'Pellet stock',
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            description: 'Pellet stock reported by the boiler (tons)',
            unit: 't',
            precision: 2,
            access: 'STATE_GET',
            reporting: {min: '10_SECONDS', max: '1_HOUR', change: 0.01},
            // The firmware reports -1000 while the boiler does not answer. Real values can be negative: ETA
            // tracks the stock as a counter that must be corrected after each pellet delivery.
            fzConvert: (model, msg, publish, options, meta) => {
                const v = msg.data.presentValue;
                if (v === undefined) return;
                return {pellet_stock: v <= -1000 || Number.isNaN(v) ? null : Math.round(v * 100) / 100};
            },
        }),

        // Heating mode (endpoint 21). "reset" hands control back to the physical switch.
        // No feedback from the boiler: the value shows the last command sent.
        m.enumLookup({
            name: 'heating',
            endpointName: 'mode',
            label: 'Heating', // + endpoint name 'mode' -> "Heating mode"
            cluster: 'genMultistateOutput',
            attribute: 'presentValue',
            lookup: {reset_to_switch: 1, auto: 2, day: 3, night: 4},
            description: 'Heating mode command (last command sent; the boiler gives no feedback)',
            access: 'ALL',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
        }),

        // Boiler command (endpoint 22): momentary, returns to idle after the command is sent.
        m.enumLookup({
            name: 'boiler',
            endpointName: 'command',
            label: 'Boiler', // + endpoint name 'command' -> "Boiler command"
            cluster: 'genMultistateOutput',
            attribute: 'presentValue',
            // No charge_hot_water: this plant has no hot water tank (the firmware still accepts state 4).
            lookup: {idle: 1, boiler_on: 2, boiler_off: 3},
            description: 'One-shot boiler command (idle = nothing); returns to idle after sending',
            access: 'ALL',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
        }),

        // Boiler error flag (endpoint 23). Held for 30 min after the last error message.
        m.binary({
            name: 'fault',
            label: 'Boiler fault',
            cluster: 'genBinaryInput',
            attribute: 'presentValue',
            valueOn: [true, 1],
            valueOff: [false, 0],
            description: 'The boiler reported an error (held for 30 minutes after the last message)',
            access: 'STATE_GET',
            entityCategory: 'diagnostic',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
        }),
    ],
};
