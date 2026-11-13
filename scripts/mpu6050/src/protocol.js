/**
 * Serial protocol parsing for the MPU-6050 telemetry dashboard.
 *
 * Accepted formats (one record per line):
 *   JSON      {"roll":1.2,"pitch":-3.4,"yaw":120, "ax":0.01, ...}
 *   CSV 9     roll,pitch,yaw,ax,ay,az,gx,gy,gz
 *   CSV 6     ax,ay,az,gx,gy,gz  (roll/pitch are derived from accel)
 *   Key/value roll=1.2 pitch=-3.4 yaw=120 ax=0.01 ...
 *
 * Units are assumed to be deg, g, deg/s and °C. All parser output keeps the
 * firmware's sign convention; visual calibration is a separate concern.
 */

const FIELD_ALIASES = {
  roll: ['roll', 'rolldeg', 'rolldegree', 'rolldegrees', 'rollangle', 'phi', '横滚', '横滚角', '翻滚角'],
  pitch: ['pitch', 'pitchdeg', 'pitchdegree', 'pitchdegrees', 'pitchangle', 'theta', '俯仰', '俯仰角'],
  yaw: ['yaw', 'yawdeg', 'yawdegree', 'yawdegrees', 'yawangle', 'heading', 'psi', '偏航', '偏航角', '航向角'],
  ax: ['ax', 'accx', 'accelx', 'accelerationx', 'axg', '加速度x'],
  ay: ['ay', 'accy', 'accely', 'accelerationy', 'ayg', '加速度y'],
  az: ['az', 'accz', 'accelz', 'accelerationz', 'azg', '加速度z'],
  gx: ['gx', 'gyrox', 'gyroscopex', 'angularvelocityx', 'gxdeg', '角速度x'],
  gy: ['gy', 'gyroy', 'gyroscopey', 'angularvelocityy', 'gydeg', '角速度y'],
  gz: ['gz', 'gyroz', 'gyroscopez', 'angularvelocityz', 'gzdeg', '角速度z'],
  temp: ['temp', 'tempc', 'temperature', 'temperaturec', 'tc', '温度', '摄氏度'],
};

const FIELD_ORDER = ['roll', 'pitch', 'yaw', 'ax', 'ay', 'az', 'gx', 'gy', 'gz', 'temp'];

const ALIAS_TO_FIELD = new Map();
for (const [field, aliases] of Object.entries(FIELD_ALIASES)) {
  for (const alias of aliases) ALIAS_TO_FIELD.set(normalizeKey(alias), field);
}

const ACCEL_ARRAY_KEYS = ['acceleration', 'accel', 'acc', 'a', '加速度'].map(normalizeKey);
const GYRO_ARRAY_KEYS = ['gyroscope', 'gyro', 'g', 'angularvelocity', '角速度'].map(normalizeKey);

export const PROTOCOL_HINTS = {
  auto: '自动识别 JSON、CSV 与键值对格式。',
  json: '每行一个 JSON 对象，例如 {"roll":1.2,"pitch":-3.4,"yaw":120,...}。',
  csv9: 'CSV 9 列：roll,pitch,yaw,ax,ay,az,gx,gy,gz。',
  csv6: 'CSV 6 列：ax,ay,az,gx,gy,gz；仅由加速度估算 Roll / Pitch。',
  kv: '键值对：roll=1.2 pitch=-3.4 yaw=120 ax=0.01 ...',
};

export function normalizeKey(key) {
  return String(key ?? '')
    .toLowerCase()
    .replace(/[^a-z0-9㐀-鿿]/g, '');
}

function toFiniteNumber(value) {
  if (typeof value === 'number') return Number.isFinite(value) ? value : null;
  if (typeof value === 'string' && value.trim() !== '') {
    const n = Number(value);
    return Number.isFinite(n) ? n : null;
  }
  return null;
}

function addNumericField(target, rawKey, rawValue) {
  const field = ALIAS_TO_FIELD.get(normalizeKey(rawKey));
  if (!field || target[field] !== undefined) return;
  const n = toFiniteNumber(rawValue);
  if (n !== null) target[field] = n;
}

function flattenJson(value, prefix, out) {
  if (Array.isArray(value)) {
    if (prefix) {
      const combined = normalizeKey(prefix);
      if (!out.has(combined)) out.set(combined, value.map(toFiniteNumber));
    }
    return;
  }
  if (!value || typeof value !== 'object') return;

  for (const [key, child] of Object.entries(value)) {
    const combined = normalizeKey(prefix + key);
    const leaf = normalizeKey(key);

    if (Array.isArray(child)) {
      const array = child.map(toFiniteNumber);
      if (!out.has(combined)) out.set(combined, array);
      // Also keep the leaf key so nested {"sensor":{"acc":[...]}} works.
      if (leaf && !out.has(leaf)) out.set(leaf, array);
    } else if (child && typeof child === 'object') {
      flattenJson(child, prefix + key, out);
    } else {
      const n = toFiniteNumber(child);
      if (n !== null) {
        if (!out.has(combined)) out.set(combined, n);
        // Nested objects often use {"attitude":{"roll":1,...}}; the leaf key
        // lets the normal field aliases match it.
        if (leaf && !out.has(leaf)) out.set(leaf, n);
      }
    }
  }
}

function sampleFromCollected(collected) {
  const sample = {};

  // Numeric named fields first.
  for (const [key, value] of collected) {
    if (typeof value !== 'number') continue;
    const field = ALIAS_TO_FIELD.get(key);
    if (field && sample[field] === undefined) sample[field] = value;
  }

  // Then support common accel/gyro array encodings: {"acc":[x,y,z],"gyro":[x,y,z]}.
  if (sample.ax === undefined && sample.ay === undefined && sample.az === undefined) {
    for (const [key, value] of collected) {
      if (!Array.isArray(value) || !ACCEL_ARRAY_KEYS.includes(key)) continue;
      if (value.length >= 3 && value.slice(0, 3).every(v => Number.isFinite(v))) {
        [sample.ax, sample.ay, sample.az] = value;
        break;
      }
    }
  }
  if (sample.gx === undefined && sample.gy === undefined && sample.gz === undefined) {
    for (const [key, value] of collected) {
      if (!Array.isArray(value) || !GYRO_ARRAY_KEYS.includes(key)) continue;
      if (value.length >= 3 && value.slice(0, 3).every(v => Number.isFinite(v))) {
        [sample.gx, sample.gy, sample.gz] = value;
        break;
      }
    }
  }

  // Explicit rotation arrays are accepted only when the order is unambiguous.
  if (sample.roll === undefined && sample.pitch === undefined && sample.yaw === undefined) {
    const rpy = collected.get('rpy') || collected.get('rollpitchyaw');
    if (Array.isArray(rpy) && rpy.length >= 3) {
      [sample.roll, sample.pitch, sample.yaw] = rpy;
    }
    const pry = collected.get('pry');
    if (sample.roll === undefined && Array.isArray(pry) && pry.length >= 3) {
      [sample.pitch, sample.roll, sample.yaw] = pry;
    }
  }

  deriveOrientationFromAccel(sample);
  return cleanSample(sample);
}

function deriveOrientationFromAccel(sample) {
  const { ax, ay, az } = sample;
  if (![ax, ay, az].every(Number.isFinite)) return;
  if (sample.roll === undefined && sample.pitch === undefined) {
    sample.roll = radToDeg(Math.atan2(ay, az));
    sample.pitch = radToDeg(Math.atan2(-ax, Math.hypot(ay, az)));
    return;
  }
  if (sample.roll === undefined) sample.roll = radToDeg(Math.atan2(ay, az));
  if (sample.pitch === undefined) sample.pitch = radToDeg(Math.atan2(-ax, Math.hypot(ay, az)));
}

function cleanSample(sample) {
  const out = {};
  for (const field of FIELD_ORDER) {
    const value = sample[field];
    if (Number.isFinite(value)) out[field] = value;
  }
  return out;
}

function sampleFromNumbers(numbers, columns) {
  if (numbers.length < columns) return null;
  const sample = {};
  if (columns === 9 || numbers.length >= 10) {
    [sample.roll, sample.pitch, sample.yaw, sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz] = numbers;
    if (numbers.length >= 10) sample.temp = numbers[9];
  } else {
    [sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz] = numbers;
  }
  deriveOrientationFromAccel(sample);
  const cleaned = cleanSample(sample);
  return Object.keys(cleaned).length ? cleaned : null;
}

function extractNumbers(text) {
  const matches = String(text).match(/[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?/g);
  return matches ? matches.map(Number).filter(Number.isFinite) : [];
}

export function parseJsonLine(line) {
  let text = String(line).trim();
  if (!text) return null;
  // Tolerate prefixes such as "DATA:{...}" or "imu: [...]".
  const brace = text.indexOf('{');
  const bracket = text.indexOf('[');
  if (bracket > 0 && (brace < 0 || bracket < brace)) {
    text = text.slice(bracket);
  } else if (brace > 0) {
    text = text.slice(brace);
  }

  let parsed;
  try {
    parsed = JSON.parse(text);
  } catch {
    return null;
  }

  if (Array.isArray(parsed)) {
    const nums = parsed.map(toFiniteNumber).filter(v => v !== null);
    if (nums.length === 9 || nums.length === 10) return sampleFromNumbers(nums, 9);
    if (nums.length === 6) return sampleFromNumbers(nums, 6);
    return null;
  }

  if (!parsed || typeof parsed !== 'object') return null;
  const collected = new Map();
  flattenJson(parsed, '', collected);
  const sample = sampleFromCollected(collected);
  return Object.keys(sample).length ? sample : null;
}

const KV_RE = /([a-zA-Z_㐀-鿿][a-zA-Z0-9_㐀-鿿]*)\s*[:=]\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)/g;

export function parseKeyValueLine(line) {
  const collected = new Map();
  KV_RE.lastIndex = 0;
  let match;
  let recognized = 0;
  while ((match = KV_RE.exec(String(line))) !== null) {
    const key = normalizeKey(match[1]);
    const field = ALIAS_TO_FIELD.get(key);
    const value = Number(match[2]);
    if (field && Number.isFinite(value)) {
      if (!collected.has(key)) {
        collected.set(key, value);
        recognized++;
      }
    }
  }
  if (!recognized) return null;
  const sample = sampleFromCollected(collected);
  return Object.keys(sample).length ? sample : null;
}

export function parseDelimitedLine(line, columns) {
  const nums = extractNumbers(line);
  if (columns === 9) {
    if (nums.length < 9) return null;
    return sampleFromNumbers(nums.slice(0, 10), 9);
  }
  if (columns === 6) {
    if (nums.length < 6) return null;
    return sampleFromNumbers(nums.slice(0, 6), 6);
  }
  return null;
}

/**
 * Parse one complete serial line.
 * @param {string} line
 * @param {'auto'|'json'|'csv9'|'csv6'|'kv'} mode
 * @returns {{ok: true, sample: object, format: string} | {ok: false, error: string}}
 */
export function parseLine(line, mode = 'auto') {
  const text = String(line ?? '').replace(/﻿/g, '').trim();
  if (!text) return { ok: false, error: '空行' };

  let sample = null;
  let format = mode;

  if (mode === 'json') {
    sample = parseJsonLine(text);
    format = 'json';
  } else if (mode === 'kv') {
    sample = parseKeyValueLine(text);
    format = 'kv';
  } else if (mode === 'csv9') {
    sample = parseDelimitedLine(text, 9);
    format = 'csv9';
  } else if (mode === 'csv6') {
    sample = parseDelimitedLine(text, 6);
    format = 'csv6';
  } else {
    // Auto mode. JSON first, then key/value, then naked numeric CSV.
    if (text.startsWith('{') || text.startsWith('[') || text.includes('{')) {
      sample = parseJsonLine(text);
      if (sample) format = 'json';
    }
    if (!sample && (text.includes(':') || text.includes('='))) {
      sample = parseKeyValueLine(text);
      if (sample) format = 'kv';
    }
    if (!sample) {
      const nums = extractNumbers(text);
      if (nums.length === 9 || nums.length === 10) {
        sample = sampleFromNumbers(nums, 9);
        format = 'csv9';
      } else if (nums.length === 6) {
        sample = sampleFromNumbers(nums, 6);
        format = 'csv6';
      }
    }
  }

  if (!sample || !Object.keys(sample).length) {
    return { ok: false, error: '未识别到有效的传感器字段' };
  }
  return { ok: true, sample, format };
}

export function radToDeg(rad) {
  return (rad * 180) / Math.PI;
}

export function wrapDegrees(deg) {
  if (!Number.isFinite(deg)) return null;
  let v = deg % 360;
  if (v > 180) v -= 360;
  if (v <= -180) v += 360;
  return v;
}

export function compassDegrees(deg) {
  const v = wrapDegrees(deg);
  return v === null ? null : (v + 360) % 360;
}

export { FIELD_ORDER };
