#!/usr/bin/env node
// Install (or update) the ETA PE-25 dashboard in Home Assistant.
//
//   export HA_URL=http://homeassistant.local:8123     # no trailing slash
//   export HA_TOKEN=<long-lived access token>          # Profile -> Security -> Long-lived tokens
//   npm install
//   node install-dashboard.mjs dashboard.en.yaml [--prefix my_device_name] [--url-path eta-pellets] [--title "Pellet boiler"]
//
// --prefix   entity prefix = Zigbee2MQTT friendly name in snake_case (default: eta_pelletkessel)
// --dry-run  print the resulting dashboard JSON, change nothing
//
// Needs Node >= 22 (built-in WebSocket). Only touches the dashboard given by --url-path.
import { readFileSync } from 'node:fs';
import { parse } from 'yaml';

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const file = args.find((a, i) => !a.startsWith('--') && (i === 0 || !args[i - 1].startsWith('--')));
if (!file) { console.error('usage: node install-dashboard.mjs <dashboard.yaml> [--prefix X] [--url-path X] [--title X] [--dry-run]'); process.exit(2); }

const prefix = opt('--prefix', 'eta_pelletkessel');
const urlPath = opt('--url-path', 'eta-pellets');
const dry = args.includes('--dry-run');

let text = readFileSync(file, 'utf8');
if (prefix !== 'eta_pelletkessel') text = text.replaceAll('eta_pelletkessel', prefix);
const config = parse(text);
const title = opt('--title', config.title ?? 'ETA');
config.title = title;

if (dry) { console.log(JSON.stringify(config, null, 2)); process.exit(0); }
if (!process.env.HA_URL || !process.env.HA_TOKEN) { console.error('Set HA_URL and HA_TOKEN'); process.exit(2); }

const ws = new WebSocket(process.env.HA_URL.replace(/^http/, 'ws') + '/api/websocket');
let id = 0; const pending = new Map();
const call = (msg) => new Promise((res) => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ ...msg, id: i })); });
const fail = (m) => { console.error(m); process.exit(1); };

ws.onerror = (e) => fail('WebSocket error: ' + (e.message ?? e));
ws.onmessage = async (ev) => {
  const m = JSON.parse(ev.data);
  if (m.type === 'auth_required') ws.send(JSON.stringify({ type: 'auth', access_token: process.env.HA_TOKEN }));
  else if (m.type === 'auth_invalid') fail('Authentication failed (check HA_TOKEN)');
  else if (m.type === 'auth_ok') {
    const list = await call({ type: 'lovelace/dashboards/list' });
    if (!list.success) fail('Cannot list dashboards: ' + JSON.stringify(list.error));
    if (!list.result.some((d) => d.url_path === urlPath)) {
      const r = await call({ type: 'lovelace/dashboards/create', url_path: urlPath, title, icon: 'mdi:fire',
                             show_in_sidebar: true, require_admin: false, mode: 'storage' });
      if (!r.success) fail('Cannot create dashboard: ' + JSON.stringify(r.error));
      console.log(`created dashboard "${title}" at /${urlPath}`);
    } else {
      console.log(`dashboard /${urlPath} exists, updating its content`);
    }
    const s = await call({ type: 'lovelace/config/save', url_path: urlPath, config });
    if (!s.success) fail('Cannot save dashboard: ' + JSON.stringify(s.error));
    console.log(`saved. Open ${process.env.HA_URL}/${urlPath}`);
    ws.close();
  } else if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
};
