import uPlot from 'uplot';
import { SERIES_COLORS, CHART_INK, CHART_GRID, CHART_TICK } from './palette.js';

const CHART_DEFS = [
  {
    id: 'attitude',
    mountId: 'chart-attitude',
    unit: '°',
    decimals: 0,
    minSpan: 45,
    series: [
      { key: 'roll', label: 'Roll', tone: 'x' },
      { key: 'pitch', label: 'Pitch', tone: 'y' },
      { key: 'yaw', label: 'Yaw', tone: 'z' },
    ],
  },
  {
    id: 'accel',
    mountId: 'chart-accel',
    unit: 'g',
    decimals: 2,
    minSpan: 2,
    series: [
      { key: 'ax', label: 'X', tone: 'x' },
      { key: 'ay', label: 'Y', tone: 'y' },
      { key: 'az', label: 'Z', tone: 'z' },
    ],
  },
  {
    id: 'gyro',
    mountId: 'chart-gyro',
    unit: '°/s',
    decimals: 0,
    minSpan: 100,
    series: [
      { key: 'gx', label: 'X', tone: 'x' },
      { key: 'gy', label: 'Y', tone: 'y' },
      { key: 'gz', label: 'Z', tone: 'z' },
    ],
  },
];

const SYNC_KEY = 'mpu6050-live-charts';

function pad(value, length = 2) {
  return String(value).padStart(length, '0');
}

function formatRelativeTick(seconds, latest) {
  const delta = Math.round(seconds - latest);
  return delta === 0 ? '0s' : `${delta}s`;
}

function formatTooltipTime(seconds) {
  if (!Number.isFinite(seconds)) return '--:--:--';
  const date = new Date(seconds * 1000);
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}.${pad(date.getMilliseconds(), 3)}`;
}

function niceStep(raw) {
  if (!(raw > 0) || !Number.isFinite(raw)) return 1;
  const exponent = Math.floor(Math.log10(raw));
  const base = 10 ** exponent;
  const fraction = raw / base;
  let nice;
  if (fraction <= 1) nice = 1;
  else if (fraction <= 2) nice = 2;
  else if (fraction <= 2.5) nice = 2.5;
  else if (fraction <= 5) nice = 5;
  else nice = 10;
  return nice * base;
}

function symmetricRange(min, max, minSpan) {
  if (!Number.isFinite(min) || !Number.isFinite(max)) return [-minSpan, minSpan];
  const peak = Math.max(Math.abs(min), Math.abs(max), minSpan) * 1.08;
  const step = niceStep(peak / 4);
  const bound = Math.ceil(peak / step) * step;
  return [-bound, bound];
}

function formatY(value, decimals) {
  if (!Number.isFinite(value)) return '';
  const text = Math.abs(value) < 10 ** (-(decimals + 1)) ? '0' : value.toFixed(decimals);
  return text === '-0' ? '0' : text;
}

function formatValue(value, decimals) {
  if (value == null || !Number.isFinite(value)) return '—';
  const text = value.toFixed(decimals);
  return text === '-0' || text === '-0.00' ? text.slice(1) : text;
}

function setText(node, text) {
  node.textContent = text;
}

export class ChartGroup {
  constructor({ windowSeconds = 30 } = {}) {
    this.windowSeconds = windowSeconds;
    this.charts = [];
    this.latestT = null;
    this.lastBuilt = { latest: null, size: -1, window: -1 };
    this.resizeObservers = [];
    this.mount();
  }


  mount() {
    for (const def of CHART_DEFS) {
      const mount = document.getElementById(def.mountId);
      if (!mount) continue;
      const emptyNode = mount.querySelector('.chart-empty');
      const tooltip = mount.querySelector('.chart-tooltip');
      const width = Math.max(180, Math.floor(mount.clientWidth || 300));
      const height = Math.max(60, Math.floor(mount.clientHeight || 120));

      const chart = {
        def,
        mount,
        emptyNode,
        tooltip,
        tooltipRows: new Map(),
        u: null,
      };

      this._buildTooltipDom(chart);

      const series = [
        {
          label: '时间',
          value: (u, v) => (Number.isFinite(v) ? formatTooltipTime(v) : '—'),
        },
        ...def.series.map(seriesDef => ({
          label: seriesDef.label,
          stroke: SERIES_COLORS[seriesDef.tone],
          width: 2,
          points: { show: false },
          spanGaps: false,
        })),
      ];

      const opts = {
        width,
        height,
        padding: [7, 9, 0, 0],
        legend: { show: false },
        cursor: {
          sync: { key: SYNC_KEY, setSeries: false },
          drag: { setScale: false, x: false, y: false },
          points: { size: 8, width: 2 },
          focus: { prox: 22 },
          y: false,
        },
        select: { show: false },
        scales: {
          x: {
            time: true,
            range: () => {
              const latest = this.latestT ?? Date.now() / 1000;
              return [latest - this.windowSeconds, latest];
            },
          },
          y: {
            range: (u, min, max) => symmetricRange(min, max, def.minSpan),
          },
        },
        axes: [
          {
            stroke: CHART_INK,
            grid: { stroke: CHART_GRID, width: 1 },
            ticks: { stroke: CHART_TICK, width: 1, size: 3 },
            font: '500 10px "Cascadia Mono", Consolas, monospace',
            size: 23,
            space: 52,
            splits: (u, axisIdx, scaleMin, scaleMax) => {
              // Keep a stable six-column reference grid for the rolling window,
              // including both the left and right edge labels.
              const count = 5;
              return Array.from({ length: count + 1 }, (_, index) => (
                scaleMin + ((scaleMax - scaleMin) * index) / count
              ));
            },
            values: (u, splits) => {
              const latest = this.latestT ?? Date.now() / 1000;
              return splits.map(seconds => formatRelativeTick(seconds, latest));
            },
          },
          {
            stroke: CHART_INK,
            grid: { stroke: CHART_GRID, width: 1 },
            ticks: { stroke: CHART_TICK, width: 1, size: 3 },
            font: '500 10px "Cascadia Mono", Consolas, monospace',
            size: 42,
            space: 38,
            values: (u, splits) => splits.map(value => formatY(value, def.decimals)),
          },
        ],
        series,
        hooks: {
          setCursor: [u => this._positionTooltip(chart, u)],
        },
      };

      const u = new uPlot(opts, this._emptyData(), mount);
      chart.u = u;
      this.charts.push(chart);

      const resizeObserver = new ResizeObserver(() => {
        const rect = mount.getBoundingClientRect();
        const w = Math.floor(rect.width);
        const h = Math.floor(rect.height);
        if (w > 40 && h > 40) {
          try {
            u.setSize({ width: w, height: h });
          } catch {
            // uPlot can throw while the container is being torn down.
          }
        }
      });
      resizeObserver.observe(mount);
      this.resizeObservers.push(resizeObserver);
    }

    // A mouse leaving one chart should clear the tooltip everywhere.
    for (const chart of this.charts) {
      chart.mount.addEventListener('mouseleave', () => this._hideAllTooltips());
    }
  }

  _buildTooltipDom(chart) {
    const tooltip = chart.tooltip;
    tooltip.replaceChildren();
    const time = document.createElement('div');
    time.className = 'tt-time';
    tooltip.appendChild(time);
    chart.tooltipTime = time;

    for (const seriesDef of chart.def.series) {
      const row = document.createElement('div');
      row.className = 'tt-row';
      const name = document.createElement('span');
      name.className = 'tt-name';
      const key = document.createElement('i');
      key.className = `tt-key series-${seriesDef.tone}`;
      key.setAttribute('aria-hidden', 'true');
      name.append(key, document.createTextNode(seriesDef.label));
      const value = document.createElement('span');
      value.className = 'tt-val';
      value.textContent = '—';
      row.append(name, value);
      tooltip.appendChild(row);
      chart.tooltipRows.set(seriesDef.key, value);
    }
  }

  _emptyData() {
    const now = Date.now() / 1000;
    this.latestT = now;
    const x = [now - this.windowSeconds, now];
    return [x, ...CHART_DEFS[0].series.map(() => [null, null])];
  }

  _emptyDataForChart(def) {
    const now = this.latestT ?? Date.now() / 1000;
    const x = [now - this.windowSeconds, now];
    return [x, ...def.series.map(() => [null, null])];
  }

  setWindow(seconds) {
    if (![10, 30, 60].includes(seconds) || seconds === this.windowSeconds) return;
    this.windowSeconds = seconds;
    this.lastBuilt.window = -1;
    this.refresh();
  }

  update(buffer, { force = false } = {}) {
    const latest = buffer.latestTime();
    if (!force && latest === this.lastBuilt.latest && buffer.length === this.lastBuilt.size && this.windowSeconds === this.lastBuilt.window) {
      return;
    }
    this.lastBuilt = { latest, size: buffer.length, window: this.windowSeconds };

    const data = buffer.visible(this.windowSeconds);
    this.latestT = data.x.length ? data.x[data.x.length - 1] : (latest ?? Date.now() / 1000);

    for (const chart of this.charts) {
      const chartData = data.x.length
        ? [data.x, ...chart.def.series.map(seriesDef => data.values[seriesDef.key])]
        : this._emptyDataForChart(chart.def);
      chart.u.setData(chartData, true);
      chart.emptyNode.hidden = data.x.length > 0;
    }
  }

  refresh(buffer) {
    if (buffer) {
      this.update(buffer, { force: true });
      return;
    }
    this.lastBuilt.window = -1;
  }

  _positionTooltip(chart, u) {
    const idx = u.cursor.idx;
    if (idx == null || !u.data?.[0]?.[idx]) {
      chart.tooltip.hidden = true;
      return;
    }

    const time = u.data[0][idx];
    setText(chart.tooltipTime, formatTooltipTime(time));

    for (const seriesDef of chart.def.series) {
      const seriesIndex = chart.def.series.findIndex(item => item.key === seriesDef.key) + 1;
      const rawValue = u.data[seriesIndex]?.[idx];
      const valueNode = chart.tooltipRows.get(seriesDef.key);
      if (valueNode) {
        const valid = rawValue != null && Number.isFinite(rawValue);
        setText(valueNode, valid
          ? `${formatValue(rawValue, chart.def.decimals)}${chart.def.unit ? ` ${chart.def.unit}` : ''}`
          : '—');
      }
    }

    const mount = chart.mount;
    const overRect = u.over.getBoundingClientRect();
    const mountRect = mount.getBoundingClientRect();
    const cursorX = overRect.left - mountRect.left + (u.cursor.left ?? 0);
    const cursorY = overRect.top - mountRect.top + (u.cursor.top ?? 10);
    const tooltip = chart.tooltip;
    tooltip.hidden = false;
    tooltip.style.visibility = 'hidden';

    const tw = tooltip.offsetWidth;
    const th = tooltip.offsetHeight;
    const margin = 4;
    let left = cursorX + 14;
    if (left + tw > mountRect.width - margin) left = cursorX - tw - 14;
    left = Math.max(margin, Math.min(left, mountRect.width - tw - margin));
    let top = cursorY - th / 2;
    top = Math.max(margin, Math.min(top, mountRect.height - th - margin));

    tooltip.style.left = `${Math.round(left)}px`;
    tooltip.style.top = `${Math.round(top)}px`;
    tooltip.style.visibility = 'visible';
  }

  _hideAllTooltips() {
    for (const chart of this.charts) chart.tooltip.hidden = true;
  }

  destroy() {
    for (const observer of this.resizeObservers) observer.disconnect();
    for (const chart of this.charts) chart.u?.destroy();
    this.resizeObservers = [];
    this.charts = [];
  }
}
