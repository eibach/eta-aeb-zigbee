// Sanity tests for pe25_zb.mjs against the real zigbee-herdsman-converters library.
// Run: npm install && npm test   (the pinned version is the one the converter was developed against)
import assert from 'node:assert/strict';
import def from './pe25_zb.mjs';
import * as zhc from 'zigbee-herdsman-converters';

const api = zhc.default ?? zhc;
api.addExternalDefinition({...def});
const dev = {modelID: 'PE25-ZB', manufacturerName: 'ETA', endpoints: [], getEndpoint: () => undefined,
             applicationVersion: 1, isDeviceEndpoints: true, customClusters: {}};
const d = await api.findByDevice(dev);
assert.ok(d, 'definition matches modelID PE25-ZB');
assert.equal(d.model, 'PE25-ZB');

let ex = d.exposes; if (typeof ex === 'function') ex = ex(dev, {});
const props = ex.map((e) => e.property ?? e.name);
const expected = ['temperature_boiler', 'temperature_flue_gas', 'temperature_buffer_top', 'temperature_buffer_bottom',
  'temperature_dhw_tank', 'temperature_dhw_tank_bottom', 'temperature_outside', 'temperature_flow_direct',
  'temperature_flow_mixer0', 'temperature_external_heat', 'pellet_stock', 'heating_mode', 'boiler_command', 'fault'];
assert.deepEqual(props, expected, 'exposed properties');

const boilerCmd = ex.find((e) => (e.property ?? e.name) === 'boiler_command');
assert.deepEqual(boilerCmd.values, ['idle', 'boiler_on', 'boiler_off'], 'boiler command options');

const epMap = d.endpoint(dev);
assert.equal(epMap.boiler, 10); assert.equal(epMap.external_heat, 19);
assert.equal(epMap.mode, 21); assert.equal(epMap.command, 22);

const msg = (cluster, ep, data) => ({data, endpoint: {ID: ep}, cluster, type: 'attributeReport', device: dev});
const conv = (cluster, ep, data) => {
  const fz = d.fromZigbee.find((f) => f.cluster === cluster);
  return fz.convert(d, msg(cluster, ep, data), () => {}, {}, {device: dev});
};
// temperatures: degC*100 on the wire, 0x8000 = invalid -> null
assert.deepEqual(conv('msTemperatureMeasurement', 10, {measuredValue: 6540}), {temperature_boiler: 65.4});
assert.deepEqual(conv('msTemperatureMeasurement', 16, {measuredValue: -450}), {temperature_outside: -4.5});
assert.deepEqual(conv('msTemperatureMeasurement', 10, {measuredValue: -32768}), {temperature_boiler: null});
assert.equal(conv('msTemperatureMeasurement', 99, {measuredValue: 100}), undefined);
// pellet stock (tons): real values may be negative; <= -1000 or NaN means unknown
assert.deepEqual(conv('genAnalogInput', 20, {presentValue: 1.85}), {pellet_stock: 1.85});
assert.deepEqual(conv('genAnalogInput', 20, {presentValue: -0.99}), {pellet_stock: -0.99});
assert.deepEqual(conv('genAnalogInput', 20, {presentValue: -1000}), {pellet_stock: null});
assert.deepEqual(conv('genAnalogInput', 20, {presentValue: NaN}), {pellet_stock: null});

console.log('converter tests passed (' + props.length + ' exposes)');
