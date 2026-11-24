import assert from 'node:assert/strict';
import {test} from 'node:test';
import {connectFitnessWatch} from '../site/watch-ble.mjs';

function mockWatch(t, {missing = false, ageSeconds = 0, disconnectDuringFetch = false,
                      missingScreens = false, screenOrder = [1, 3, 1, 2],
                      disconnectDuringScreenLookup = false} = {}) {
 const packet = new DataView(new ArrayBuffer(12));
 packet.setUint8(0, 1);
 const writes = [];
 const screenWrites = [];
 const device = new EventTarget();
 device.gatt = {
  connected: true,
  connect: async () => ({getPrimaryService: async () => ({
   async getCharacteristic(uuid) {
    if (uuid.startsWith('abcdef06') && missing) throw new DOMException('Missing', 'NotFoundError');
    const screen = uuid.startsWith('abcdef07');
    if (screen && missingScreens) throw new DOMException('Missing', 'NotFoundError');
    if (screen && disconnectDuringScreenLookup) device.gatt.disconnect();
    return {
     addEventListener() {},
     async startNotifications() {},
     async readValue() { return screen ? new DataView(Uint8Array.from(screenOrder).buffer) : packet; },
     async writeValueWithResponse(data) {
      if (uuid.startsWith('abcdef06')) writes.push([...new Uint8Array(data.buffer)]);
      if (screen) screenWrites.push([...new Uint8Array(data.buffer, data.byteOffset, data.byteLength)]);
     },
    };
   },
  })}),
  disconnect() {
   this.connected = false;
   device.dispatchEvent(new Event('gattserverdisconnected'));
  },
 };
 const originalNavigator = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
 Object.defineProperty(globalThis, 'navigator', {configurable: true, value: {
  bluetooth: {requestDevice: async () => device},
  geolocation: {getCurrentPosition() { assert.fail('A fixed station never needs device location'); }},
 }});
 t.after(() => {
  if (originalNavigator) Object.defineProperty(globalThis, 'navigator', originalNavigator);
  else delete globalThis.navigator;
 });
 t.mock.method(Date, 'now', () => 1795512600000);
 const fetchAqi = t.mock.method(globalThis, 'fetch', async address => {
  assert.equal(address, './aqi.json');
  if (disconnectDuringFetch) device.gatt.disconnect();
  return {ok: true, json: async () => ({
   observedAt: Math.floor(Date.now() / 1000) - ageSeconds, aqi: 300,
  })};
 });
 return {writes, fetchAqi, screenWrites};
}

test('writes station AQI and remaining validity to the watch in little-endian bytes', async t => {
 const hw = mockWatch(t, {ageSeconds: 7140});
 const watch = await connectFitnessWatch({onPulse() {}});
 assert.equal(hw.fetchAqi.mock.callCount(), 0);
 await watch.syncAqi();
 assert.deepEqual(hw.writes, [[2, 44, 1, 60, 0]]);
});

test('older firmware can connect without fetching an AQI it cannot receive', async t => {
 const hw = mockWatch(t, {missing: true});
 const watch = await connectFitnessWatch();
 await assert.rejects(watch.syncAqi(), /firmware/i);
 assert.equal(hw.fetchAqi.mock.callCount(), 0);
});

test('does not write stale AQI to the watch', async t => {
 const hw = mockWatch(t, {ageSeconds: 7201});
 const watch = await connectFitnessWatch();
 await assert.rejects(watch.syncAqi(), /recent/i);
 assert.deepEqual(hw.writes, []);
});

test('a sync that outlives the Bluetooth connection never writes to the watch', async t => {
 const hw = mockWatch(t, {disconnectDuringFetch: true});
 const watch = await connectFitnessWatch();
 await assert.rejects(watch.syncAqi(), /disconnected/i);
 assert.deepEqual(hw.writes, []);
});

test('reads the watch screen order and applies a different order or Time only', async t => {
 const hw = mockWatch(t);
 const watch = await connectFitnessWatch();
 assert.deepEqual(await watch.getScreenOrder(), [3, 1, 2]);
 await watch.setScreenOrder([2, 3, 0]);
 await watch.setScreenOrder([0]);
 assert.deepEqual(hw.screenWrites, [[1, 2, 3, 0], [1, 0]]);
});

test('rejects empty, duplicated or unknown screen IDs before writing', async t => {
 const hw = mockWatch(t);
 const watch = await connectFitnessWatch();
 for (const invalid of [[], [1, 1], [4], [-1], [0.5]]) {
  await assert.rejects(watch.setScreenOrder(invalid), /screen order/i);
 }
 assert.deepEqual(hw.screenWrites, []);
});

test('older firmware still connects and reports that screen controls require an update', async t => {
 mockWatch(t, {missingScreens: true});
 const watch = await connectFitnessWatch();
 assert.equal(await watch.getScreenOrder(), null);
 await assert.rejects(watch.setScreenOrder([0]), /firmware/i);
});

test('does not write screen settings after its watch disconnected', async t => {
 const hw = mockWatch(t, {disconnectDuringScreenLookup: true});
 const watch = await connectFitnessWatch();
 await assert.rejects(watch.setScreenOrder([0]), /disconnected/i);
 assert.deepEqual(hw.screenWrites, []);
});
