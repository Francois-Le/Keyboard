const test = require('node:test');
const assert = require('node:assert/strict');
const trace = require('../src/trace.js');

function installDom() {
  const elements = new Map();
  class Element {
    constructor(id) {
      this.id = id;
      this.disabled = false;
      this.checked = true;
      this.value = '';
      this.textContent = '';
      this.className = '';
      this.children = [];
      this.style = {};
      this.classList = { toggle: () => {} };
      this.scrollTop = 0;
      this.scrollHeight = 0;
    }
    addEventListener(type, handler) { this[`on${type}`] = handler; }
    appendChild(child) { this.children.push(child); this.scrollHeight += 1; return child; }
    append(...children) { this.children.push(...children); }
    remove() {}
    closest() { return new Element('label'); }
    set innerHTML(_) { this.children = []; }
    get innerHTML() { return ''; }
  }
  const document = {
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, new Element(id));
      return elements.get(id);
    },
    createElement(tag) { return new Element(tag); },
    body: new Element('body')
  };
  Object.defineProperty(globalThis, 'document', { value: document, configurable: true });
  Object.defineProperty(globalThis, 'requestAnimationFrame', { value: cb => setTimeout(cb, 0), configurable: true });
  Object.defineProperty(globalThis, 'setTimeout', { value: setTimeout, configurable: true });
  Object.defineProperty(globalThis, 'clearTimeout', { value: clearTimeout, configurable: true });
  return { elements, document };
}

function makeFrame(type, seq, micros, fillPayload) {
  const bytes = new Uint8Array(trace.FRAME_SIZE);
  bytes.set(trace.MAGIC, 0);
  bytes[4] = type;
  const view = new DataView(bytes.buffer);
  view.setUint32(8, seq, true);
  view.setBigUint64(12, BigInt(micros), true);
  fillPayload(new DataView(bytes.buffer, 20, trace.PAYLOAD_SIZE), bytes);
  view.setUint16(46, trace.crc16CcittFalse(bytes, 0, 46), true);
  return bytes;
}

function helloFrame() {
  return makeFrame(1, 1, 100, p => {
    p.setUint8(0, 1); p.setUint8(1, 2); p.setUint8(2, 2); p.setUint8(3, 0);
    p.setUint32(4, 1, true); p.setUint32(8, 0, true); p.setUint32(12, 1, true); p.setUint32(16, 1, true); p.setUint32(20, 77, true);
  });
}

function makePort({ closeReject = false } = {}) {
  let pending;
  const reader = {
    released: false,
    cancelCalls: 0,
    read() {
      return new Promise(resolve => { pending = resolve; });
    },
    cancel() {
      this.cancelCalls += 1;
      if (pending) pending({ done: true });
      return Promise.resolve();
    },
    releaseLock() { this.released = true; }
  };
  const port = {
    openOptions: null,
    signals: null,
    closeCalls: 0,
    readable: { getReader: () => reader },
    open(options) { this.openOptions = options; return Promise.resolve(); },
    setSignals(signals) { this.signals = signals; return Promise.resolve(); },
    close() {
      this.closeCalls += 1;
      return closeReject ? Promise.reject(new Error('close boom')) : Promise.resolve();
    }
  };
  return { port, reader };
}

test('live lifecycle cancels reader, releases, closes, and resets on reconnect', async () => {
  installDom();
  globalThis.KBTTrace = trace;
  const { SerialTraceApp } = require('../src/app.js');
  const first = makePort();
  const second = makePort();
  let requestCount = 0;
  Object.defineProperty(globalThis, 'navigator', {
    value: { serial: { requestPort: () => Promise.resolve(++requestCount === 1 ? first.port : second.port) } },
    configurable: true
  });
  Object.defineProperty(globalThis, 'window', { value: { isSecureContext: true }, configurable: true });

  const app = new SerialTraceApp();
  app.model.queueKnown = true;
  app.parser.push(helloFrame().slice(0, 10));
  const connectPromise = app.connect();
  await new Promise(resolve => setTimeout(resolve, 0));
  await app.disconnect();
  await connectPromise;
  assert.equal(first.port.openOptions.baudRate, 115200);
  assert.deepEqual(first.port.signals, { dataTerminalReady: true });
  assert.equal(first.reader.cancelCalls, 1);
  assert.equal(first.reader.released, true);
  assert.equal(first.port.closeCalls, 1);
  assert.equal(app.model.queueKnown, false);
  assert.equal(app.parser.buffer.length, 0);

  const secondConnect = app.connect();
  await new Promise(resolve => setTimeout(resolve, 0));
  await app.disconnect();
  await secondConnect;
  assert.equal(second.port.closeCalls, 1);
});

test('close failure remains visible', async () => {
  const { elements } = installDom();
  globalThis.KBTTrace = trace;
  const { SerialTraceApp } = require('../src/app.js');
  const fake = makePort({ closeReject: true });
  Object.defineProperty(globalThis, 'navigator', {
    value: { serial: { requestPort: () => Promise.resolve(fake.port) } },
    configurable: true
  });
  Object.defineProperty(globalThis, 'window', { value: { isSecureContext: true }, configurable: true });
  const app = new SerialTraceApp();
  const connectPromise = app.connect();
  await new Promise(resolve => setTimeout(resolve, 0));
  await app.disconnect();
  await connectPromise;
  assert.match(elements.get('status').textContent, /Port close failed/);
  assert.equal(app.port, fake.port);
  assert.equal(elements.get('connectBtn').disabled, true);
  assert.equal(elements.get('disconnectBtn').disabled, false);
});

for (const mode of ['EOF', 'error']) {
  test(`serial ${mode} closes even when readable still references the ended stream`, async () => {
    const { elements } = installDom();
    globalThis.KBTTrace = trace;
    const { SerialTraceApp } = require('../src/app.js');
    let closes = 0;
    const stream = new ReadableStream({
      start(controller) {
        if (mode === 'EOF') controller.close();
        else controller.error(new Error('unplug'));
      }
    });
    const port = {
      readable: stream,
      open: async () => {},
      setSignals: async () => {},
      close: async () => { assert.equal(stream.locked, false); closes++; }
    };
    Object.defineProperty(globalThis, 'navigator', { value: { serial: { requestPort: async () => port } }, configurable: true });
    Object.defineProperty(globalThis, 'window', { value: { isSecureContext: true }, configurable: true });
    const app = new SerialTraceApp();
    await app.connect();
    assert.equal(closes, 1);
    assert.equal(app.port, null);
    assert.equal(elements.get('connectBtn').disabled, false);
    if (mode === 'error') assert.match(elements.get('status').textContent, /unplug/);
  });
}
