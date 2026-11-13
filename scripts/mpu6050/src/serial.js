/**
 * Thin wrapper around the Web Serial API.
 *
 * Web Serial is only available in Chromium browsers on secure origins
 * (https:// or http://localhost). The browser deliberately does not expose a
 * COM-port list without user permission; callers first use requestPort().
 */

export function isSerialSupported() {
  return typeof navigator !== 'undefined' && 'serial' in navigator && !!navigator.serial;
}

export function serialSupportMessage() {
  if (typeof window !== 'undefined' && !window.isSecureContext) {
    return '当前页面不是安全上下文。请通过 start.bat 使用 http://localhost 打开。';
  }
  if (!isSerialSupported()) {
    return '当前浏览器不支持 Web Serial。请使用桌面版 Chrome 或 Edge，并通过 start.bat 启动页面。';
  }
  return '';
}

export function describePort(port, index) {
  let info = {};
  try {
    info = port.getInfo?.() || {};
  } catch {
    info = {};
  }
  const usb = [info.usbVendorId, info.usbProductId]
    .filter(value => Number.isFinite(value))
    .map(value => value.toString(16).toUpperCase().padStart(4, '0'))
    .join(':');
  return usb ? `串口 #${index + 1} · USB ${usb}` : `串口 #${index + 1} · 已授权`;
}

function friendlyOpenError(error) {
  switch (error?.name) {
    case 'NotFoundError':
      return '串口设备不存在或已被拔出。';
    case 'NetworkError':
      return '无法打开串口，可能被其他程序占用。请关闭串口监视器后重试。';
    case 'SecurityError':
      return '浏览器拒绝访问该串口，请重新授权。';
    case 'InvalidStateError':
      return '该串口已经打开。';
    case 'NotReadableError':
      return '串口不可读，请检查设备状态。';
    default:
      return `打开串口失败：${error?.message || error || '未知错误'}`;
  }
}

export class SerialConnection {
  constructor({ onLine, onState, onError } = {}) {
    this.onLine = onLine || (() => {});
    this.onState = onState || (() => {});
    this.onError = onError || (() => {});

    this.port = null;
    this.reader = null;
    this.decoder = null;
    this.lineBuffer = '';
    this.reading = false;
    this._readLoopPromise = null;
    this._deviceLost = false;

    this._bindDeviceEvents();
  }

  get connected() {
    return !!this.port;
  }

  _bindDeviceEvents() {
    if (!isSerialSupported()) return;
    this._onPhysicalDisconnect = event => {
      const lostPort = event?.port || event?.target;
      if (!this.port || (lostPort && lostPort !== this.port)) return;
      this._deviceLost = true;
      this.onError('串口设备已断开。');
      this.disconnect();
    };
    try {
      navigator.serial.addEventListener('disconnect', this._onPhysicalDisconnect);
    } catch {
      // Older Chromium versions still surface read errors; the read loop covers it.
    }
  }

  async getAuthorizedPorts() {
    if (!isSerialSupported()) return [];
    return navigator.serial.getPorts();
  }

  async requestPort() {
    if (!isSerialSupported()) throw new Error(serialSupportMessage());
    return navigator.serial.requestPort();
  }

  async connect(port, baudRate) {
    if (!isSerialSupported()) throw new Error(serialSupportMessage());
    if (!port) throw new Error('请先选择一个串口。');
    if (this.port) throw new Error('串口已经连接。');

    try {
      await port.open({
        baudRate,
        dataBits: 8,
        stopBits: 1,
        parity: 'none',
        flowControl: 'none',
        bufferSize: 65536,
      });
    } catch (error) {
      throw new Error(friendlyOpenError(error));
    }

    this.port = port;
    this.decoder = new TextDecoder();
    this.lineBuffer = '';
    this.reading = true;
    this._deviceLost = false;
    this.onState('connected');
    this._readLoopPromise = this._readLoop();
  }

  async disconnect() {
    if (!this.port && !this.reading) return;
    this.reading = false;

    if (this.reader) {
      try {
        await this.reader.cancel();
      } catch {
        // The port may already be gone.
      }
    }

    if (this._readLoopPromise) {
      try {
        await this._readLoopPromise;
      } catch {
        // Already reported by the read loop.
      }
      this._readLoopPromise = null;
    } else {
      await this._closePort();
    }
  }

  async _closePort() {
    if (!this.port) return;
    try {
      await this.port.close();
    } catch {
      // Device may have been unplugged; closing an invalid handle is harmless.
    }
    this.port = null;
  }

  async _readLoop() {
    let reader = null;
    try {
      if (!this.port?.readable) throw new Error('串口不可读。');
      reader = this.port.readable.getReader();
      this.reader = reader;

      while (this.reading) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value && value.length) this._consumeBytes(value);
      }
    } catch (error) {
      if (this.reading && !this._deviceLost) {
        const message = error?.name === 'NetworkError'
          ? '串口连接中断。'
          : `串口读取失败：${error?.message || error || '未知错误'}`;
        this.onError(message);
      }
    } finally {
      this.reading = false;
      if (reader) {
        try {
          this.reader = null;
          reader.releaseLock();
        } catch {
          // Ignore.
        }
      }
      this._flushLine();
      await this._closePort();
      this.decoder = null;
      this.onState('disconnected');
    }
  }

  _consumeBytes(bytes) {
    const text = this.decoder ? this.decoder.decode(bytes, { stream: true }) : '';
    if (!text) return;
    this.lineBuffer += text;

    // Accept CRLF, LF and bare CR terminators.
    const parts = this.lineBuffer.split(/\r\n|\n|\r/);
    this.lineBuffer = parts.pop() ?? '';
    for (const line of parts) {
      if (line.length) this.onLine(line);
    }

    if (this.lineBuffer.length > 65536) {
      this.lineBuffer = '';
      this.onError('接收缓冲区溢出，已丢弃一段没有换行符的数据。');
    }
  }

  _flushLine() {
    if (this.decoder) {
      try {
        this.lineBuffer += this.decoder.decode();
      } catch {
        // Ignore decoder state on disconnect.
      }
    }
    if (this.lineBuffer.trim().length) this.onLine(this.lineBuffer);
    this.lineBuffer = '';
  }

  dispose() {
    if (isSerialSupported() && this._onPhysicalDisconnect) {
      try {
        navigator.serial.removeEventListener('disconnect', this._onPhysicalDisconnect);
      } catch {
        // Ignore.
      }
    }
    this.disconnect();
  }
}
