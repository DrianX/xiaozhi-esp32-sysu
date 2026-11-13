export const TELEMETRY_FIELDS = ['roll', 'pitch', 'yaw', 'ax', 'ay', 'az', 'gx', 'gy', 'gz', 'temp'];

const CSV_HEADER = [
  'timestamp_iso',
  'roll_deg',
  'pitch_deg',
  'yaw_deg',
  'ax_g',
  'ay_g',
  'az_g',
  'gx_dps',
  'gy_dps',
  'gz_dps',
  'temp_c',
];

const DECIMALS = {
  roll: 3,
  pitch: 3,
  yaw: 3,
  ax: 5,
  ay: 5,
  az: 5,
  gx: 3,
  gy: 3,
  gz: 3,
  temp: 2,
};

/**
 * Fixed-capacity ring buffer. Keeps telemetry history for the live charts,
 * table view and CSV export without unbounded memory growth.
 */
export class TelemetryBuffer {
  constructor(capacity = 60000) {
    this.capacity = capacity;
    this.head = 0;
    this.size = 0;
    this.times = new Float64Array(capacity);
    this.values = {};
    for (const field of TELEMETRY_FIELDS) this.values[field] = new Float32Array(capacity);
  }

  clear() {
    this.head = 0;
    this.size = 0;
  }

  push(sample, timestamp = Date.now() / 1000) {
    const idx = this.head;
    this.times[idx] = timestamp;
    for (const field of TELEMETRY_FIELDS) {
      const value = Number(sample[field]);
      this.values[field][idx] = Number.isFinite(value) ? value : NaN;
    }
    this.head = (this.head + 1) % this.capacity;
    if (this.size < this.capacity) this.size++;
  }

  get length() {
    return this.size;
  }

  latestTime() {
    if (!this.size) return null;
    return this.times[(this.head - 1 + this.capacity) % this.capacity];
  }

  latest() {
    if (!this.size) return null;
    const idx = (this.head - 1 + this.capacity) % this.capacity;
    const out = { t: this.times[idx] };
    for (const field of TELEMETRY_FIELDS) out[field] = this.values[field][idx];
    return out;
  }

  /**
   * Build arrays for the visible time window, newest point on the right.
   * Missing values become null so uPlot can leave a gap.
   *
   * If a high-rate device produces more points than the chart can meaningfully
   * display, stride down to maxPoints. The table and CSV export remain full
   * resolution.
   */
  visible(windowSeconds, maxPoints = 5000) {
    if (!this.size) {
      return { x: [], values: Object.fromEntries(TELEMETRY_FIELDS.map(field => [field, []])) };
    }
    const latestT = this.latestTime();
    const cutoff = latestT - windowSeconds;
    const start = (this.head - this.size + this.capacity) % this.capacity;
    const x = [];
    const values = Object.fromEntries(TELEMETRY_FIELDS.map(field => [field, []]));

    let first = 0;
    while (first < this.size && this.times[(start + first) % this.capacity] < cutoff) first++;

    const count = this.size - first;
    const stride = Math.max(1, Math.ceil(count / Math.max(1, maxPoints)));
    const lastIndex = (start + this.size - 1) % this.capacity;

    for (let offset = first; offset < this.size; offset += stride) {
      const idx = (start + offset) % this.capacity;
      x.push(this.times[idx]);
      for (const field of TELEMETRY_FIELDS) {
        const v = this.values[field][idx];
        values[field].push(Number.isFinite(v) ? v : null);
      }
    }

    // Always include the newest sample, even when the stride lands short.
    if (x.length === 0 || x[x.length - 1] !== this.times[lastIndex]) {
      x.push(this.times[lastIndex]);
      for (const field of TELEMETRY_FIELDS) {
        const v = this.values[field][lastIndex];
        values[field].push(Number.isFinite(v) ? v : null);
      }
    }
    return { x, values };
  }

  /** Newest first; used by the accessible table view. */
  rows(limit = 200) {
    const count = Math.min(limit, this.size);
    const rows = [];
    for (let n = 0; n < count; n++) {
      const idx = (this.head - 1 - n + this.capacity * 2) % this.capacity;
      const row = { t: this.times[idx] };
      for (const field of TELEMETRY_FIELDS) {
        const value = this.values[field][idx];
        row[field] = Number.isFinite(value) ? value : null;
      }
      rows.push(row);
    }
    return rows;
  }

  toCSV() {
    const lines = [CSV_HEADER.join(',')];
    if (!this.size) return `${lines.join('\n')}\n`;
    const start = (this.head - this.size + this.capacity) % this.capacity;
    for (let i = 0; i < this.size; i++) {
      const idx = (start + i) % this.capacity;
      const cells = [new Date(this.times[idx] * 1000).toISOString()];
      for (const field of TELEMETRY_FIELDS) {
        const value = this.values[field][idx];
        cells.push(Number.isFinite(value) ? value.toFixed(DECIMALS[field]) : '');
      }
      lines.push(cells.join(','));
    }
    return `${lines.join('\n')}\n`;
  }
}
