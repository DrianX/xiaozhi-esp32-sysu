import { TelemetryBuffer } from './buffer.js';
import { parseLine, PROTOCOL_HINTS, wrapDegrees } from './protocol.js';
import { ChartGroup } from './charts.js';
import { AttitudeView } from './model-view.js';
import {
  SerialConnection,
  isSerialSupported,
  serialSupportMessage,
  describePort,
} from './serial.js';

const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => Array.from(root.querySelectorAll(selector));

const PREFS_KEY = 'mpu6050-dashboard-prefs-v2';
const LEGACY_PREFS_KEY = 'mpu6050-dashboard-prefs-v1';
const MODEL_VERSION = '2026-11-13-meshopt1';
const DEFAULT_PREFS = {
  baud: 115200,
  protocol: 'auto',
  chartWindow: 30,
  calibration: { invertRoll: false, invertPitch: false, invertYaw: false },
};

const refs = {
  globalState: $('#global-state'),
  globalStateText: $('#global-state-text'),
  globalRate: $('#global-rate'),
  demoBtn: $('#demo-btn'),
  helpOpen: $('#help-open'),
  serialWarning: $('#serial-warning'),
  serialSupportTag: $('#serial-support-tag'),
  portSelect: $('#port-select'),
  portPick: $('#port-pick'),
  portRefresh: $('#port-refresh'),
  baudInput: $('#baud-input'),
  protocolSelect: $('#protocol-select'),
  portHint: $('#port-hint'),
  connectBtn: $('#connect-btn'),
  connectLabel: $('#connect-label'),
  connState: $('#conn-state'),
  rateValue: $('#rate-value'),
  ignoredValue: $('#ignored-value'),
  rawPanel: $('#raw-panel'),
  rawLog: $('#raw-log'),
  rawCount: $('#raw-count'),
  telemetryPanel: $('#telemetry-panel'),
  lastUpdate: $('#last-update'),
  valRoll: $('#val-roll'),
  valPitch: $('#val-pitch'),
  valYaw: $('#val-yaw'),
  meterRoll: $('#meter-roll'),
  meterPitch: $('#meter-pitch'),
  meterYaw: $('#meter-yaw'),
  knobRoll: $('#knob-roll'),
  knobPitch: $('#knob-pitch'),
  knobYaw: $('#knob-yaw'),
  valAx: $('#val-ax'),
  valAy: $('#val-ay'),
  valAz: $('#val-az'),
  valGx: $('#val-gx'),
  valGy: $('#val-gy'),
  valGz: $('#val-gz'),
  tempValue: $('#temp-value'),
  sampleCount: $('#sample-count'),
  uptimeValue: $('#uptime-value'),
  viewerPanel: $('#viewer-panel'),
  viewerCanvas: $('#viewer-canvas'),
  viewerLive: $('#viewer-live'),
  viewerLiveText: $('#viewer-live-text'),
  viewerEmpty: $('#viewer-empty'),
  viewerFallback: $('#viewer-fallback'),
  modelSource: $('#model-source'),
  headingValue: $('#heading-value'),
  frozenBadge: $('#frozen-badge'),
  freezeBtn: $('#freeze-btn'),
  resetView: $('#reset-view'),
  fullscreenBtn: $('#fullscreen-btn'),
  invertRoll: $('#invert-roll'),
  invertPitch: $('#invert-pitch'),
  invertYaw: $('#invert-yaw'),
  windowControl: $('#window-control'),
  tableOpen: $('#table-open'),
  exportCsv: $('#export-csv'),
  tableDialog: $('#table-dialog'),
  tableDialogSub: $('#table-dialog-sub'),
  tableBody: $('#data-table-body'),
  helpDialog: $('#help-dialog'),
  toastRegion: $('#toast-region'),
};

const buffer = new TelemetryBuffer();
let charts = null;
let view = null;
let serial = null;

const state = {
  supported: isSerialSupported(),
  ports: [],
  selectedPort: null,
  connected: false,
  connecting: false,
  disconnecting: false,
  demo: false,
  demoTimer: null,
  demoStart: 0,
  lastDemoRaw: -1,
  rate: 0,
  rateCount: 0,
  rateStamp: performance.now(),
  ignored: 0,
  rawLines: [],
  rawDirty: false,
  lastValues: {},
  lastSampleWall: 0,
  sessionActive: false,
  sessionStart: 0,
  frozen: false,
  protocol: DEFAULT_PREFS.protocol,
  baud: DEFAULT_PREFS.baud,
  chartWindow: DEFAULT_PREFS.chartWindow,
  calibration: { ...DEFAULT_PREFS.calibration },
  errorAt: 0,
  lastTableRender: 0,
};

let prefs = { ...DEFAULT_PREFS };

/* ---------- preferences ---------- */

function loadPreferences() {
  try {
    let raw = localStorage.getItem(PREFS_KEY);

    // One-time migration from the earlier default of 1152000. Keep the user's
    // other preferences, but let a stored old default fall back to 115200.
    if (!raw) {
      const legacyRaw = localStorage.getItem(LEGACY_PREFS_KEY);
      if (legacyRaw) {
        try {
          const legacy = JSON.parse(legacyRaw);
          if (legacy && typeof legacy === 'object') {
            if (Number(legacy.baud) === 1152000) delete legacy.baud;
            localStorage.setItem(PREFS_KEY, JSON.stringify(legacy));
            raw = localStorage.getItem(PREFS_KEY);
          }
        } catch {
          // Ignore a malformed legacy record.
        }
      }
    }

    if (!raw) return;
    const parsed = JSON.parse(raw);
    const baud = Number(parsed.baud);
    if (Number.isInteger(baud) && baud >= 300 && baud <= 20_000_000) prefs.baud = baud;
    if (['auto', 'json', 'csv9', 'csv6', 'kv'].includes(parsed.protocol)) prefs.protocol = parsed.protocol;
    if ([10, 30, 60].includes(Number(parsed.chartWindow))) prefs.chartWindow = Number(parsed.chartWindow);
    if (parsed.calibration && typeof parsed.calibration === 'object') {
      prefs.calibration = {
        invertRoll: !!parsed.calibration.invertRoll,
        invertPitch: !!parsed.calibration.invertPitch,
        invertYaw: !!parsed.calibration.invertYaw,
      };
    }
  } catch {
    // Preferences are a convenience; defaults are fine if storage is blocked.
  }
}

function savePreferences() {
  prefs = {
    baud: state.baud,
    protocol: state.protocol,
    chartWindow: state.chartWindow,
    calibration: { ...state.calibration },
  };
  try {
    localStorage.setItem(PREFS_KEY, JSON.stringify(prefs));
  } catch {
    // Ignore storage quota/privacy failures.
  }
}

/* ---------- small utilities ---------- */

function pad(value, length = 2) {
  return String(value).padStart(length, '0');
}

function formatClockMs(seconds) {
  if (!Number.isFinite(seconds)) return '--:--:--.---';
  const date = new Date(seconds * 1000);
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}.${pad(date.getMilliseconds(), 3)}`;
}

function formatSigned(value, decimals = 1) {
  if (!Number.isFinite(value)) return '—';
  let text = value.toFixed(decimals);
  if (Number.parseFloat(text) === 0) text = (0).toFixed(decimals);
  return value > 0 ? `+${text}` : text;
}

function formatHeading(value) {
  if (!Number.isFinite(value)) return '—';
  return `${formatSigned(value, 1)}°`;
}

function showToast(message, tone = 'info', timeout = 4200) {
  const toast = document.createElement('div');
  toast.className = 'toast';
  toast.dataset.tone = tone;
  toast.textContent = message;
  refs.toastRegion.appendChild(toast);
  window.setTimeout(() => {
    toast.style.opacity = '0';
    toast.style.transform = 'translateY(4px)';
    toast.style.transition = 'opacity .18s ease, transform .18s ease';
    window.setTimeout(() => toast.remove(), 200);
  }, timeout);
}

function setGlobalState(tone, text) {
  if (refs.globalState.dataset.tone !== tone) refs.globalState.dataset.tone = tone;
  if (refs.globalStateText.textContent !== text) refs.globalStateText.textContent = text;
}

function setViewerLive(tone, text) {
  if (refs.viewerLive.dataset.tone !== tone) refs.viewerLive.dataset.tone = tone;
  if (refs.viewerLiveText.textContent !== text) refs.viewerLiveText.textContent = text;
}

function currentAge() {
  return state.lastSampleWall ? (performance.now() - state.lastSampleWall) / 1000 : Infinity;
}

function elapsedLabel(seconds) {
  if (seconds < 0.85) return '刚刚更新';
  if (seconds < 60) return `${seconds.toFixed(seconds < 10 ? 1 : 0)} s 前`;
  return `${Math.floor(seconds / 60)} min 前`;
}

function formatDuration(milliseconds) {
  const total = Math.max(0, Math.floor(milliseconds / 1000));
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${pad(minutes)}:${pad(seconds)}`;
}

function localFileStamp(date = new Date()) {
  return `${date.getFullYear()}${pad(date.getMonth() + 1)}${pad(date.getDate())}_${pad(date.getHours())}${pad(date.getMinutes())}${pad(date.getSeconds())}`;
}

function sanitizeSample(sample) {
  const limits = {
    roll: 100000, pitch: 100000, yaw: 100000,
    ax: 10000, ay: 10000, az: 10000,
    gx: 1000000, gy: 1000000, gz: 1000000,
    temp: 10000,
  };
  const clean = {};
  for (const [field, value] of Object.entries(sample)) {
    const limit = limits[field];
    if (limit === undefined || !Number.isFinite(value)) continue;
    if (Math.abs(value) <= limit) clean[field] = value;
  }
  return clean;
}

/* ---------- telemetry UI ---------- */

function setMeter(axis, value, range) {
  const map = {
    roll: { fill: refs.meterRoll, knob: refs.knobRoll },
    pitch: { fill: refs.meterPitch, knob: refs.knobPitch },
    yaw: { fill: refs.meterYaw, knob: refs.knobYaw },
  };
  const nodes = map[axis];
  if (!nodes) return;
  const valid = Number.isFinite(value);
  const display = valid ? wrapDegrees(value) : null;
  const p = valid ? Math.max(0, Math.min(100, ((display + range) / (range * 2)) * 100)) : 50;
  const left = Math.min(50, p);
  const width = Math.abs(p - 50);
  nodes.fill.style.left = `${left}%`;
  nodes.fill.style.width = `${width}%`;
  nodes.knob.style.left = `${p}%`;
  nodes.fill.style.opacity = valid ? '0.68' : '0';
  nodes.knob.style.opacity = valid ? '1' : '0';
}

function setVectorValue(node, value, decimals) {
  node.textContent = Number.isFinite(value) ? value.toFixed(decimals) : '—';
}

function renderTelemetry() {
  const sample = state.lastValues;
  const hasData = !!state.lastSampleWall;
  const age = currentAge();
  const stale = hasData && age > 2.5;
  refs.telemetryPanel.classList.toggle('is-stale', stale);

  refs.valRoll.textContent = formatSigned(sample.roll, 1);
  refs.valPitch.textContent = formatSigned(sample.pitch, 1);
  refs.valYaw.textContent = formatSigned(wrapDegrees(sample.yaw), 1);
  setMeter('roll', sample.roll, 180);
  setMeter('pitch', sample.pitch, 90);
  setMeter('yaw', sample.yaw, 180);

  setVectorValue(refs.valAx, sample.ax, 2);
  setVectorValue(refs.valAy, sample.ay, 2);
  setVectorValue(refs.valAz, sample.az, 2);
  setVectorValue(refs.valGx, sample.gx, 1);
  setVectorValue(refs.valGy, sample.gy, 1);
  setVectorValue(refs.valGz, sample.gz, 1);

  refs.tempValue.textContent = Number.isFinite(sample.temp) ? sample.temp.toFixed(1) : '—';
  refs.sampleCount.textContent = buffer.length ? buffer.length.toLocaleString('zh-CN') : '0';
  refs.uptimeValue.textContent = state.sessionActive && state.sessionStart
    ? formatDuration(performance.now() - state.sessionStart)
    : '00:00';

  if (!hasData) {
    refs.lastUpdate.textContent = '等待数据';
  } else if (age < 0.85) {
    refs.lastUpdate.textContent = '刚刚更新';
  } else {
    refs.lastUpdate.textContent = elapsedLabel(age);
  }

  const heading = view && view.hasAttitude
    ? wrapDegrees(view.getHeading())
    : (Number.isFinite(sample.yaw) ? wrapDegrees(sample.yaw) : null);
  refs.headingValue.textContent = hasData && heading !== null ? formatHeading(heading) : '—';

  refs.viewerEmpty.classList.toggle('is-hidden', hasData);
  refs.frozenBadge.hidden = !state.frozen;
}

/* ---------- connection / status ---------- */

function updateControls() {
  const busy = state.connecting || state.disconnecting;
  const lockedByLink = state.connected || busy;
  const demoLock = state.demo;

  refs.demoBtn.disabled = state.connected || state.connecting || state.disconnecting;
  refs.demoBtn.setAttribute('aria-pressed', state.demo ? 'true' : 'false');
  refs.demoBtn.querySelector('span').textContent = state.demo ? '停止演示' : '演示数据';

  refs.portSelect.disabled = !state.supported || lockedByLink || demoLock || state.ports.length === 0;
  refs.portPick.disabled = !state.supported || lockedByLink || demoLock;
  refs.portRefresh.disabled = !state.supported || lockedByLink || demoLock;
  refs.baudInput.disabled = !state.supported || lockedByLink || demoLock;

  if (state.connected) {
    refs.connectBtn.disabled = busy;
    refs.connectBtn.classList.add('danger');
    refs.connectLabel.textContent = state.disconnecting ? '正在断开…' : '断开串口';
    refs.connectBtn.querySelector('.btn-led').style.opacity = '0.9';
  } else {
    refs.connectBtn.disabled = !state.supported || busy || demoLock || !state.selectedPort;
    refs.connectBtn.classList.remove('danger');
    refs.connectLabel.textContent = !state.supported
      ? '浏览器不支持'
      : state.demo
        ? '演示模式中'
        : state.connecting ? '正在连接…' : '连接串口';
    refs.connectBtn.querySelector('.btn-led').style.opacity = '0.55';
  }
}

function updateStatus() {
  const age = currentAge();
  const rate = `${state.rate.toFixed(1)} Hz`;
  refs.globalRate.textContent = rate;
  refs.rateValue.textContent = rate;

  if (state.demo) {
    setGlobalState('demo', '演示数据');
    setViewerLive(state.frozen ? 'frozen' : 'demo', state.frozen ? '姿态已冻结' : '本地演示');
  } else if (state.connecting) {
    setGlobalState('connecting', '正在连接');
    setViewerLive('idle', '连接中');
  } else if (state.connected) {
    if (age < 1.6) {
      setGlobalState('live', '串口已连接');
      setViewerLive(state.frozen ? 'frozen' : 'live', state.frozen ? '姿态已冻结' : '实时数据');
    } else {
      setGlobalState('waiting', '等待数据');
      setViewerLive(state.frozen ? 'frozen' : 'paused', state.frozen ? '姿态已冻结' : '等待数据');
    }
  } else if (state.errorAt > 0 && performance.now() - state.errorAt < 6000) {
    setGlobalState('error', '连接失败');
    setViewerLive('idle', state.lastSampleWall ? '数据已暂停' : '等待数据');
  } else {
    setGlobalState('idle', '未连接');
    setViewerLive('idle', state.lastSampleWall ? '数据已暂停' : '等待数据');
  }

  refs.frozenBadge.hidden = !state.frozen;

  if (state.connected) {
    refs.connState.textContent = state.disconnecting ? '断开中' : '已连接';
    refs.connState.dataset.tone = 'good';
  } else if (state.connecting) {
    refs.connState.textContent = '连接中';
    refs.connState.dataset.tone = 'warning';
  } else if (state.errorAt > 0 && performance.now() - state.errorAt < 6000) {
    refs.connState.textContent = '连接失败';
    refs.connState.dataset.tone = 'error';
  } else {
    refs.connState.textContent = state.demo ? '演示模式' : '待连接';
    refs.connState.dataset.tone = '';
  }
  refs.ignoredValue.textContent = state.ignored.toLocaleString('zh-CN');
}

/* ---------- session data ---------- */

function resetSession() {
  buffer.clear();
  state.lastValues = {};
  state.ignored = 0;
  state.rawLines = [];
  state.rawDirty = false;
  state.lastSampleWall = 0;
  state.rateCount = 0;
  state.rate = 0;
  state.rateStamp = performance.now();
  refs.rawLog.textContent = '尚无数据';
  refs.rawCount.textContent = '0';
  refs.ignoredValue.textContent = '0';
  refs.tableDialogSub.textContent = '最新 200 条采样 · 实时更新';
  view?.clearAttitude();
  charts?.update(buffer, { force: true });
  renderTelemetry();
  updateStatus();
}

function handleSample(sample, format = '') {
  const clean = sanitizeSample(sample);
  if (!Object.keys(clean).length) {
    state.ignored++;
    return;
  }

  Object.assign(state.lastValues, clean);
  buffer.push(state.lastValues, Date.now() / 1000);
  state.rateCount++;
  state.lastSampleWall = performance.now();

  if (view && (Number.isFinite(state.lastValues.roll) || Number.isFinite(state.lastValues.pitch) || Number.isFinite(state.lastValues.yaw))) {
    view.setAttitude(state.lastValues);
  }
}

function parseIncomingLine(line) {
  const result = parseLine(line, state.protocol);
  if (!result.ok) {
    state.ignored++;
    return;
  }
  handleSample(result.sample, result.format);
}

function sanitizeRawLine(line) {
  const text = String(line)
    .replace(/\u001b\[[0-9;]*m/g, '')
    .replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g, '');
  return text.length > 900 ? `${text.slice(0, 900)}…` : text;
}

function logRaw(line) {
  state.rawLines.push({ t: Date.now(), line: sanitizeRawLine(line) });
  if (state.rawLines.length > 250) state.rawLines.splice(0, state.rawLines.length - 250);
  state.rawDirty = true;
  refs.rawCount.textContent = String(state.rawLines.length);
}

function handleRawLine(line) {
  const text = String(line)
    .replace(/\u001b\[[0-9;]*m/g, '')
    .replace(/\u0000/g, '');
  logRaw(text);
  parseIncomingLine(text);
}

function renderRawLog() {
  state.rawDirty = false;
  if (!state.rawLines.length) {
    refs.rawLog.textContent = '尚无数据';
    return;
  }
  const nearBottom = refs.rawLog.scrollTop + refs.rawLog.clientHeight >= refs.rawLog.scrollHeight - 8;
  const text = state.rawLines.map(item => `${formatClockMs(item.t / 1000)}  ${item.line}`).join('\n');
  refs.rawLog.textContent = text;
  if (nearBottom) refs.rawLog.scrollTop = refs.rawLog.scrollHeight;
}

/* ---------- demo mode ---------- */

function demoTick() {
  const t = (performance.now() - state.demoStart) / 1000;
  const roll = 22 * Math.sin(t * 0.82) + 4.5 * Math.sin(t * 2.13 + 0.4);
  const pitch = 13 * Math.sin(t * 0.57 + 0.7) + 3.5 * Math.sin(t * 1.66);
  const yaw = wrapDegrees(t * 17 + 18 * Math.sin(t * 0.21));
  const r = roll * Math.PI / 180;
  const p = pitch * Math.PI / 180;

  const sample = {
    roll: roll + (Math.random() - 0.5) * 0.08,
    pitch: pitch + (Math.random() - 0.5) * 0.08,
    yaw: yaw + (Math.random() - 0.5) * 0.08,
    ax: Math.sin(p) + 0.035 * Math.sin(t * 3.2) + (Math.random() - 0.5) * 0.004,
    ay: Math.cos(p) * Math.sin(r) + 0.026 * Math.sin(t * 2.7 + 1.1) + (Math.random() - 0.5) * 0.004,
    az: Math.cos(p) * Math.cos(r) + 0.03 * Math.sin(t * 4.4) + (Math.random() - 0.5) * 0.004,
    gx: 22 * 0.82 * Math.cos(t * 0.82) + 4.5 * 2.13 * Math.cos(t * 2.13 + 0.4) + (Math.random() - 0.5) * 0.5,
    gy: -(13 * 0.57 * Math.cos(t * 0.57 + 0.7) + 3.5 * 1.66 * Math.cos(t * 1.66)) + (Math.random() - 0.5) * 0.5,
    gz: 17 + 18 * 0.21 * Math.cos(t * 0.21) + (Math.random() - 0.5) * 0.5,
    temp: 31.6 + 1.5 * Math.sin(t * 0.08) + 0.1 * Math.sin(t * 4.0) + (Math.random() - 0.5) * 0.04,
  };

  handleSample(sample, 'demo');

  const rawTick = Math.floor(t * 2);
  if (rawTick !== state.lastDemoRaw) {
    state.lastDemoRaw = rawTick;
    logRaw(`DEMO ${JSON.stringify({
      roll: +sample.roll.toFixed(2),
      pitch: +sample.pitch.toFixed(2),
      yaw: +sample.yaw.toFixed(2),
      ax: +sample.ax.toFixed(3),
      ay: +sample.ay.toFixed(3),
      az: +sample.az.toFixed(3),
      gx: +sample.gx.toFixed(1),
      gy: +sample.gy.toFixed(1),
      gz: +sample.gz.toFixed(1),
      temp: +sample.temp.toFixed(1),
    })}`);
  }
}

function startDemo() {
  if (state.connected || state.connecting) return;
  resetSession();
  state.demo = true;
  state.errorAt = 0;
  state.demoStart = performance.now();
  state.sessionStart = performance.now();
  state.sessionActive = true;
  state.rateStamp = performance.now();
  state.rateCount = 0;
  state.lastDemoRaw = -1;
  demoTick();
  state.demoTimer = window.setInterval(demoTick, 20);
  updateControls();
  updateStatus();
  renderTelemetry();
  showToast('演示模式已开启。三维模型与曲线由本地数据驱动。', 'success', 3200);
}

function stopDemo() {
  if (!state.demo) return;
  window.clearInterval(state.demoTimer);
  state.demoTimer = null;
  state.demo = false;
  state.sessionActive = false;
  state.sessionStart = 0;
  state.rate = 0;
  state.rateCount = 0;
  state.rateStamp = performance.now();
  updateControls();
  updateStatus();
  renderTelemetry();
  showToast('演示模式已停止。', 'info', 2400);
}

function toggleDemo() {
  if (state.demo) stopDemo();
  else startDemo();
}

/* ---------- serial ---------- */

function updatePortHint() {
  if (!state.supported) return;
  const count = state.ports.length;
  refs.portHint.textContent = count
    ? `已授权 ${count} 个串口 · 新设备需点「选择串口」授权`
    : '仅显示已授权串口 · 拔插后可点刷新';
}

function renderPortOptions() {
  const previous = state.selectedPort;
  refs.portSelect.replaceChildren();
  if (!state.ports.length) {
    const option = document.createElement('option');
    option.value = '';
    option.textContent = '尚无已授权串口';
    refs.portSelect.appendChild(option);
    refs.portSelect.value = '';
    state.selectedPort = null;
  } else {
    let selectedIndex = state.ports.findIndex(port => port === previous);
    if (selectedIndex < 0) selectedIndex = 0;
    state.selectedPort = state.ports[selectedIndex];
    state.ports.forEach((port, index) => {
      const option = document.createElement('option');
      option.value = String(index);
      option.textContent = describePort(port, index);
      refs.portSelect.appendChild(option);
    });
    refs.portSelect.value = String(selectedIndex);
  }
  updatePortHint();
  updateControls();
}

async function refreshPorts({ announce = false } = {}) {
  if (!state.supported) return;
  try {
    state.ports = await serial.getAuthorizedPorts();
    if (state.selectedPort && !state.ports.includes(state.selectedPort)) state.selectedPort = null;
    renderPortOptions();
    if (announce) {
      if (state.ports.length) {
        showToast(`串口列表已刷新：${state.ports.length} 个已授权串口。`, 'success', 2600);
      } else {
        showToast('未发现已授权串口。请点击「选择串口」授权新设备。', 'warning', 4500);
      }
    }
  } catch (error) {
    showToast(`读取串口列表失败：${error?.message || error}`, 'error');
  }
}

async function pickPort() {
  if (!state.supported) return;
  try {
    const port = await serial.requestPort();
    if (!state.ports.includes(port)) state.ports.push(port);
    state.selectedPort = port;
    renderPortOptions();
    if (state.selectedPort) {
      const index = state.ports.indexOf(state.selectedPort);
      refs.portSelect.value = String(index);
    }
    updateControls();
  } catch (error) {
    if (error?.name !== 'NotFoundError') {
      showToast(`选择串口失败：${error?.message || error}`, 'error');
    }
  }
}

function handleSerialState(nextState) {
  if (nextState === 'connected') {
    state.connected = true;
    state.connecting = false;
    state.disconnecting = false;
    state.errorAt = 0;
    resetSession();
    state.sessionActive = true;
    state.sessionStart = performance.now();
    state.rateStamp = performance.now();
    state.rateCount = 0;
    showToast(`串口已连接 · ${state.baud} bps`, 'success', 3000);
  } else if (nextState === 'disconnected') {
    state.connected = false;
    state.connecting = false;
    state.disconnecting = false;
    state.sessionActive = false;
    state.sessionStart = 0;
    state.rate = 0;
    state.rateCount = 0;
    state.rateStamp = performance.now();
  }
  updateControls();
  updateStatus();
  renderTelemetry();
}

async function connectSerial() {
  if (!state.selectedPort) {
    showToast('请先选择一个串口。', 'warning');
    return;
  }
  const baud = Number(refs.baudInput.value);
  if (!Number.isInteger(baud) || baud < 300 || baud > 20_000_000) {
    refs.baudInput.classList.add('invalid');
    showToast('波特率需为 300–20000000 之间的整数。', 'error');
    refs.baudInput.focus();
    return;
  }
  refs.baudInput.classList.remove('invalid');
  state.baud = baud;
  savePreferences();

  state.connecting = true;
  state.errorAt = 0;
  updateControls();
  updateStatus();
  try {
    await serial.connect(state.selectedPort, baud);
  } catch (error) {
    state.connecting = false;
    state.connected = false;
    state.errorAt = performance.now();
    updateControls();
    updateStatus();
    showToast(error?.message || '串口连接失败。', 'error', 6000);
  }
}

async function disconnectSerial() {
  if (!serial) return;
  state.disconnecting = true;
  updateControls();
  try {
    await serial.disconnect();
  } catch (error) {
    showToast(`断开串口失败：${error?.message || error}`, 'error');
  } finally {
    state.disconnecting = false;
    state.connected = false;
    updateControls();
    updateStatus();
  }
}

function initSerial() {
  if (!state.supported) return;
  serial = new SerialConnection({
    onLine: handleRawLine,
    onState: handleSerialState,
    onError: message => {
      state.errorAt = performance.now();
      showToast(message, 'error', 6000);
      updateStatus();
    },
  });

  try {
    navigator.serial.addEventListener('connect', refreshPorts);
    navigator.serial.addEventListener('disconnect', refreshPorts);
  } catch {
    // Port hotplug notifications are optional.
  }
}

/* ---------- table / export ---------- */

function valueCell(value, decimals) {
  return Number.isFinite(value) ? value.toFixed(decimals) : '—';
}

function renderTable() {
  const rows = buffer.rows(200);
  refs.tableBody.replaceChildren();
  if (!rows.length) {
    const tr = document.createElement('tr');
    tr.className = 'empty-row';
    const td = document.createElement('td');
    td.colSpan = 11;
    td.textContent = '还没有采样数据';
    tr.appendChild(td);
    refs.tableBody.appendChild(tr);
    refs.tableDialogSub.textContent = '最新 200 条采样 · 实时更新';
    return;
  }

  const fragment = document.createDocumentFragment();
  for (const row of rows) {
    const tr = document.createElement('tr');
    const cells = [
      formatClockMs(row.t),
      ...['roll', 'pitch', 'yaw'].map(key => valueCell(row[key], 1)),
      ...['ax', 'ay', 'az'].map(key => valueCell(row[key], 2)),
      ...['gx', 'gy', 'gz'].map(key => valueCell(row[key], 1)),
      valueCell(row.temp, 1),
    ];
    for (const cellText of cells) {
      const td = document.createElement('td');
      td.textContent = cellText;
      tr.appendChild(td);
    }
    fragment.appendChild(tr);
  }
  refs.tableBody.appendChild(fragment);
  refs.tableDialogSub.textContent = `最新 ${rows.length} 条采样 · 实时更新`;
  state.lastTableRender = performance.now();
}

function openTable() {
  renderTable();
  refs.tableDialog.showModal();
}

function closeDialog(dialog) {
  if (dialog?.open) dialog.close();
}

function exportCsv() {
  if (!buffer.length) {
    showToast('暂无可导出的数据。', 'warning');
    return;
  }
  const blob = new Blob([buffer.toCSV()], { type: 'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = `mpu6050_${localFileStamp()}.csv`;
  document.body.appendChild(link);
  link.click();
  link.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1500);
  showToast(`已导出 ${buffer.length.toLocaleString('zh-CN')} 条采样。`, 'success', 3000);
}

/* ---------- 3D view ---------- */

function initView() {
  try {
    view = new AttitudeView(refs.viewerCanvas);
    view.setCalibration(state.calibration);
  } catch (error) {
    refs.viewerPanel.dataset.model = 'unavailable';
    refs.viewerFallback.hidden = false;
    refs.viewerFallback.textContent = '无法初始化 WebGL。请确认浏览器已启用硬件加速，然后刷新页面。三维视图不可用时，其余遥测功能仍可使用。';
    refs.modelSource.textContent = '模型 · 不可用';
    refs.freezeBtn.disabled = true;
    refs.resetView.disabled = true;
    refs.fullscreenBtn.disabled = true;
    for (const checkbox of [refs.invertRoll, refs.invertPitch, refs.invertYaw]) checkbox.disabled = true;
    console.error(error);
    return;
  }

  refs.viewerPanel.dataset.model = 'placeholder';
  const modelUrl = new URL(`assets/esp32.glb?v=${MODEL_VERSION}`, document.baseURI).href;
  view.loadModel(modelUrl, {
    onReady: ({ fallback }) => {
      refs.viewerPanel.dataset.model = fallback ? 'fallback' : 'ready';
      refs.modelSource.textContent = fallback ? '模型 · 简化预览' : '模型 · esp32-3D模型.step';
      if (fallback) {
        showToast('高清模型载入失败，继续使用简化预览。可查看控制台了解原因。', 'warning', 6000);
      }
    },
    onError: error => {
      console.error('3D model load failed:', error);
    },
  });
}

function bindViewControls() {
  refs.freezeBtn.addEventListener('click', () => {
    state.frozen = !state.frozen;
    refs.freezeBtn.setAttribute('aria-pressed', state.frozen ? 'true' : 'false');
    view?.setFrozen(state.frozen);
    updateStatus();
    renderTelemetry();
  });

  refs.resetView.addEventListener('click', () => view?.resetView());

  refs.fullscreenBtn.addEventListener('click', async () => {
    try {
      if (document.fullscreenElement) {
        await document.exitFullscreen();
      } else {
        await refs.viewerPanel.requestFullscreen();
      }
    } catch (error) {
      showToast(`无法切换全屏：${error?.message || error}`, 'error');
    }
  });

  document.addEventListener('fullscreenchange', () => {
    const active = !!document.fullscreenElement;
    refs.fullscreenBtn.setAttribute('aria-label', active ? '退出全屏' : '全屏查看三维模型');
    refs.fullscreenBtn.title = active ? '退出全屏' : '全屏';
    view?.resize();
  });

  for (const [input, key] of [
    [refs.invertRoll, 'invertRoll'],
    [refs.invertPitch, 'invertPitch'],
    [refs.invertYaw, 'invertYaw'],
  ]) {
    input.addEventListener('change', () => {
      state.calibration = { ...state.calibration, [key]: input.checked };
      view?.setCalibration(state.calibration);
      savePreferences();
    });
  }
}

/* ---------- UI loop ---------- */

function tickUi() {
  renderTelemetry();
  charts?.update(buffer);
  if (refs.rawPanel.open && state.rawDirty) renderRawLog();
  if (refs.tableDialog.open && performance.now() - state.lastTableRender > 950) renderTable();
  updateStatus();
}

function tickRate() {
  const now = performance.now();
  const dt = (now - state.rateStamp) / 1000;
  if (dt >= 0.45) {
    const instant = state.rateCount / dt;
    if (state.rateCount > 0) {
      state.rate = state.rate > 0 ? state.rate * 0.42 + instant * 0.58 : instant;
    } else {
      state.rate *= 0.55;
      if (state.rate < 0.05) state.rate = 0;
    }
    state.rateCount = 0;
    state.rateStamp = now;
    const text = `${state.rate.toFixed(1)} Hz`;
    refs.rateValue.textContent = text;
    refs.globalRate.textContent = text;
    refs.ignoredValue.textContent = state.ignored.toLocaleString('zh-CN');
  }
}

/* ---------- bindings ---------- */

function applyPreferences() {
  state.baud = prefs.baud;
  state.protocol = prefs.protocol;
  state.chartWindow = prefs.chartWindow;
  state.calibration = { ...DEFAULT_PREFS.calibration, ...prefs.calibration };
  refs.baudInput.value = String(state.baud);
  refs.protocolSelect.value = state.protocol;
  refs.protocolSelect.title = PROTOCOL_HINTS[state.protocol] || '';
  refs.invertRoll.checked = state.calibration.invertRoll;
  refs.invertPitch.checked = state.calibration.invertPitch;
  refs.invertYaw.checked = state.calibration.invertYaw;
  for (const button of $$('button[data-window]', refs.windowControl)) {
    const active = Number(button.dataset.window) === state.chartWindow;
    button.classList.toggle('is-active', active);
    button.setAttribute('aria-pressed', active ? 'true' : 'false');
  }
}

function bindActions() {
  refs.demoBtn.addEventListener('click', toggleDemo);
  refs.portPick.addEventListener('click', pickPort);
  refs.portRefresh.addEventListener('click', () => refreshPorts({ announce: true }));
  refs.connectBtn.addEventListener('click', () => {
    if (state.connected) disconnectSerial();
    else connectSerial();
  });

  refs.portSelect.addEventListener('change', () => {
    const index = Number(refs.portSelect.value);
    state.selectedPort = Number.isInteger(index) ? state.ports[index] ?? null : null;
    updateControls();
  });

  refs.baudInput.addEventListener('input', () => {
    refs.baudInput.classList.remove('invalid');
  });
  refs.baudInput.addEventListener('change', () => {
    const baud = Number(refs.baudInput.value);
    if (Number.isInteger(baud) && baud >= 300 && baud <= 20_000_000) {
      state.baud = baud;
      savePreferences();
    }
  });

  refs.protocolSelect.addEventListener('change', () => {
    state.protocol = refs.protocolSelect.value;
    refs.protocolSelect.title = PROTOCOL_HINTS[state.protocol] || '';
    savePreferences();
  });

  refs.windowControl.addEventListener('click', event => {
    const button = event.target.closest('button[data-window]');
    if (!button) return;
    const seconds = Number(button.dataset.window);
    if (![10, 30, 60].includes(seconds)) return;
    state.chartWindow = seconds;
    savePreferences();
    charts?.setWindow(seconds);
    charts?.update(buffer, { force: true });
    for (const item of $$('button[data-window]', refs.windowControl)) {
      const active = item === button;
      item.classList.toggle('is-active', active);
      item.setAttribute('aria-pressed', active ? 'true' : 'false');
    }
  });

  refs.rawPanel.addEventListener('toggle', () => {
    if (refs.rawPanel.open) renderRawLog();
  });

  refs.tableOpen.addEventListener('click', openTable);
  refs.exportCsv.addEventListener('click', exportCsv);

  refs.helpOpen.addEventListener('click', () => refs.helpDialog.showModal());

  for (const closeButton of $$('[data-close-dialog]')) {
    closeButton.addEventListener('click', () => closeDialog(document.getElementById(closeButton.dataset.closeDialog)));
  }

  for (const dialog of [refs.tableDialog, refs.helpDialog]) {
    dialog.addEventListener('click', event => {
      if (event.target !== dialog) return;
      const rect = dialog.getBoundingClientRect();
      const inside = event.clientX >= rect.left && event.clientX <= rect.right
        && event.clientY >= rect.top && event.clientY <= rect.bottom;
      if (!inside) dialog.close();
    });
  }

  window.addEventListener('beforeunload', () => {
    if (state.demoTimer) window.clearInterval(state.demoTimer);
  });
}

function applyBrowserSupport() {
  const message = serialSupportMessage();
  if (!message) {
    refs.serialSupportTag.textContent = 'WEB SERIAL';
    return;
  }
  state.supported = false;
  refs.serialWarning.hidden = false;
  refs.serialWarning.textContent = message;
  refs.serialSupportTag.textContent = 'UNAVAILABLE';
  refs.portSelect.disabled = true;
  refs.portPick.disabled = true;
  refs.portRefresh.disabled = true;
  refs.baudInput.disabled = true;
  refs.connectBtn.disabled = true;
  refs.connectLabel.textContent = '当前浏览器不可用';
  updatePortHint();
}

function init() {
  loadPreferences();
  applyPreferences();
  applyBrowserSupport();

  charts = new ChartGroup({ windowSeconds: state.chartWindow });
  charts.update(buffer, { force: true });

  initView();
  bindViewControls();
  bindActions();
  initSerial();

  if (state.supported) refreshPorts();
  updateControls();
  updateStatus();
  renderTelemetry();

  if (!document.fullscreenEnabled) refs.fullscreenBtn.disabled = true;

  window.setInterval(tickUi, 100);
  window.setInterval(tickRate, 500);
}

init();
