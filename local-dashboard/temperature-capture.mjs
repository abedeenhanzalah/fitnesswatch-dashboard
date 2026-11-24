export class TemperatureCapture {
 #rows = [];
 #nextRun = 1;
 #run = null;

 get active() {
  return this.#run !== null;
 }

 get sampleCount() {
  return this.#rows.length;
 }

 start(referenceC, placement, powerSource) {
  if (this.active) return;
  if (referenceC == null || String(referenceC).trim() === '' || !Number.isFinite(Number(referenceC))) {
   throw new Error('Enter a room thermometer reading in °C.');
  }
  if (!['bench', 'wrist'].includes(placement) || !['usb', 'battery'].includes(powerSource)) {
   throw new Error('Select the test placement and power source.');
  }
  this.#run = {id: this.#nextRun++, placement, powerSource, referenceC: Number(referenceC)};
 }

 stop() {
  this.#run = null;
 }

 sample(chipC, time = new Date()) {
  if (!this.active || !Number.isFinite(chipC)) return;
  const {id, placement, powerSource, referenceC} = this.#run;
  this.#rows.push([id, placement, powerSource, time.toISOString(), chipC, referenceC]);
 }

 toCsv() {
  const header = 'run,placement,power_source,timestamp_utc,chip_c,reference_c';
  return [header, ...this.#rows.map(row => row.join(','))].join('\r\n') + '\r\n';
 }
}
