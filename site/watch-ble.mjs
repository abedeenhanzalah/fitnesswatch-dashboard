// Firmware telemetry for FitnessWatch. Call connectFitnessWatch from a user gesture.
const SERVICE = '12345678-1234-5678-1234-56789abcdef0';
const characteristicId = number => 'abcdef0' + number + '-1234-5678-1234-56789abcdef0';
export const WATCH_SCREEN_NAMES = Object.freeze(['Time', 'BPM', 'Steps', 'IIT Delhi AQI']);

function validateScreenOrder(order) {
 if (order.length === 0 || order.length > WATCH_SCREEN_NAMES.length ||
     new Set(order).size !== order.length ||
     order.some(page => !Number.isInteger(page) || page < 0 || page >= WATCH_SCREEN_NAMES.length)) {
  throw new Error('Invalid screen order. Select at least one screen and include each screen once.');
 }
}

function decodeScreenOrder(packet) {
 if (packet.byteLength < 2 || packet.getUint8(0) !== 1) {
  throw new Error('This firmware returned an unsupported screen order.');
 }
 const order = Array.from(new Uint8Array(packet.buffer, packet.byteOffset + 1, packet.byteLength - 1));
 validateScreenOrder(order);
 return order;
}

function check(packet, length) {
 if (packet.byteLength !== length || packet.getUint8(0) !== 1) {
  throw new Error('Unsupported FitnessWatch packet');
 }
}

export function decodePulse(packet) {
 check(packet, 12);
 const flags = packet.getUint8(1);
 const spo2 = packet.getUint8(10);
 const oxygenTrial = packet.getUint8(11);
 return {
  opticalReady: Boolean(flags & 1),
  bpm: flags & 2 ? packet.getUint16(8, true) : null,
  bpmHeld: Boolean(flags & 16),
  spo2: spo2 || null,
  oxygenTrialSupported: Boolean(flags & 8),
  oxygenTrial: (flags & 8) && oxygenTrial >= 70 && oxygenTrial <= 100 ? oxygenTrial : null,
  uptimeSeconds: packet.getUint32(4, true),
 };
}

function decodeRaw(packet) {
 check(packet, 10);
 if (!packet.getUint8(1)) return null;
 return {red: packet.getUint32(2, true), ir: packet.getUint32(6, true)};
}

function decodeMotion(packet) {
 // The first firmware uses version 0 when initialization or a sensor read fails.
 if (packet.byteLength === 13 && packet.getUint8(0) === 0) return null;
 check(packet, 13);
 const xyz = offset => [0, 2, 4].map(i => packet.getInt16(offset + i, true) / 1000);
 return {accel: xyz(1), gyro: xyz(7)};
}

export function decodeActivity(packet) {
 check(packet, 14);
 const flags = packet.getUint8(1);
 return {
  steps: flags & 1 ? packet.getUint32(2, true) : null,
  epochSeconds: flags & 2 ? packet.getUint32(6, true) : null,
  utcOffsetMinutes: flags & 2 ? packet.getInt16(10, true) : null,
  moving: Boolean(flags & 4),
  redWeak: Boolean(flags & 8),
  imuTemperatureC: flags & 16 ? packet.getInt16(12, true) / 100 : null,
 };
}

export function encodeClock(date) {
 const packet = new DataView(new ArrayBuffer(7));
 packet.setUint8(0, 1);
 packet.setUint32(1, Math.floor(date.getTime() / 1000), true);
 packet.setInt16(5, -date.getTimezoneOffset(), true);
 return packet;
}

async function fetchIitDelhiAqi() {
 let reading;
 try {
  const response = await fetch('./aqi.json', {cache: 'no-cache', signal: AbortSignal.timeout(12000)});
  if (!response.ok) throw new Error();
  reading = await response.json();
 } catch {
  throw new Error('Could not load the IIT Delhi station reading. Try again.');
 }
 const ageSeconds = Math.ceil(Date.now() / 1000 - reading?.observedAt);
 if (!Number.isInteger(reading?.aqi) || reading.aqi < 0 || reading.aqi > 500 ||
     !Number.isInteger(reading.observedAt) || ageSeconds >= 7200 || ageSeconds < -300) {
  throw new Error('No recent IIT Delhi station AQI is available.');
 }
 return {aqi: reading.aqi, validSeconds: 7200 - Math.max(0, ageSeconds)};
}

async function optionalCharacteristic(service, number) {
 try {
  return await service.getCharacteristic(characteristicId(number));
 } catch (error) {
  if (error.name === 'NotFoundError') return null;
  throw error;
 }
}

async function subscribe(characteristic, decode, receive, onError) {
 if (!characteristic || !receive) return;
 characteristic.addEventListener('characteristicvaluechanged', event => {
  try {
   receive(decode(event.target.value));
  } catch (error) {
   onError?.(error);
  }
 });
 await characteristic.startNotifications();
 receive(decode(await characteristic.readValue()));
}

export async function connectFitnessWatch(callbacks = {}) {
 if (!globalThis.navigator?.bluetooth) {
  throw new Error('Open the app in a browser with Bluetooth support over HTTPS or localhost.');
 }
 const device = await navigator.bluetooth.requestDevice({filters: [{services: [SERVICE]}]});
 let closed = false;
 let operationPending = false;
 const onDisconnect = () => {
  closed = true;
  callbacks.onDisconnect?.();
 };
 const requireConnection = () => {
  if (closed || !device.gatt.connected) throw new Error('The watch disconnected. Connect again.');
 };
 const withWatchOperation = async action => {
  requireConnection();
  if (operationPending) throw new Error('Another watch action is in progress.');
  operationPending = true;
  try {
   const result = await action();
   requireConnection();
   return result;
  } finally {
   operationPending = false;
  }
 };
 device.addEventListener('gattserverdisconnected', onDisconnect, {once: true});
 try {
  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(SERVICE);
  const clock = await optionalCharacteristic(service, 5);
  if (clock) await clock.writeValueWithResponse(encodeClock(new Date()));
  await subscribe(await service.getCharacteristic(characteristicId(1)),
                  decodePulse, callbacks.onPulse, callbacks.onError);
  if (callbacks.onMotion) {
   await subscribe(await optionalCharacteristic(service, 2), decodeMotion, callbacks.onMotion, callbacks.onError);
  }
  if (callbacks.onRaw) {
   await subscribe(await optionalCharacteristic(service, 3), decodeRaw, callbacks.onRaw, callbacks.onError);
  }
  if (callbacks.onActivity) {
   await subscribe(await optionalCharacteristic(service, 4), decodeActivity, callbacks.onActivity, callbacks.onError);
  }
  return {
   disconnect() {
    closed = true;
    device.gatt.disconnect();
   },
   syncAqi() {
    return withWatchOperation(async () => {
     const characteristic = await optionalCharacteristic(service, 6);
     requireConnection();
     if (!characteristic) throw new Error('Install the AQI firmware on the watch to enable syncing.');
     const {aqi, validSeconds} = await fetchIitDelhiAqi();
     requireConnection();
     const packet = new DataView(new ArrayBuffer(5));
     packet.setUint8(0, 2);
     packet.setUint16(1, aqi, true);
     packet.setUint16(3, validSeconds, true);
     try {
      await characteristic.writeValueWithResponse(packet);
     } catch (error) {
      requireConnection();
      throw new Error('Could not send AQI. Make sure the watch has the IIT Delhi AQI firmware.', {cause: error});
     }
    });
   },
   getScreenOrder() {
    return withWatchOperation(async () => {
     const characteristic = await optionalCharacteristic(service, 7);
     requireConnection();
     return characteristic ? decodeScreenOrder(await characteristic.readValue()) : null;
    });
   },
   async setScreenOrder(order) {
    validateScreenOrder(order);
    const packet = new Uint8Array([1, ...order]);
    return withWatchOperation(async () => {
     const characteristic = await optionalCharacteristic(service, 7);
     requireConnection();
     if (!characteristic) throw new Error('Install the latest watch firmware to use screen controls.');
     await characteristic.writeValueWithResponse(packet);
    });
   },
  };
 } catch (error) {
  device.removeEventListener('gattserverdisconnected', onDisconnect);
  if (device.gatt.connected) device.gatt.disconnect();
  throw error;
 }
}
