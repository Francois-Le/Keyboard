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

test('layout preference persists, relabels capture export and restores on startup', () => {
  const { elements } = installDom();
  globalThis.KBTTrace = trace;
  const { SerialTraceApp, demoFrames } = require('../src/app.js');
  const storage = new Map();
  Object.defineProperty(globalThis, 'window', {
    value: { localStorage: { getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value) } },
    configurable: true
  });
  const app = new SerialTraceApp();
  app.restoreLayout();
  assert.equal(app.keyboardLayout, 'en-US');
  for (const bytes of demoFrames()) for (const record of app.parser.push(bytes)) app.model.applyRecord(record);
  app.setKeyboardLayout('fr-FR');
  assert.equal(elements.get('keyboardLayout').value, 'fr-FR');
  assert.equal(app.captureExport().displayLayout, 'fr-FR');
  assert.equal(app.captureExport().events.find(event => event.kind === 'OUTPUT').title, "{ (AltGr, ')");
  const restored = new SerialTraceApp();
  restored.restoreLayout();
  assert.equal(restored.keyboardLayout, 'fr-FR');
});

test('blocked storage does not prevent layout selection and shows a notice', () => {
  const { elements } = installDom();
  const { SerialTraceApp } = require('../src/app.js');
  const window = {};
  Object.defineProperty(window, 'localStorage', { get() { throw new Error('blocked'); } });
  Object.defineProperty(globalThis, 'window', { value: window, configurable: true });
  const app = new SerialTraceApp();
  app.restoreLayout();
  app.setKeyboardLayout('fr-FR');
  assert.equal(app.keyboardLayout, 'fr-FR');
  assert.match(elements.get('layoutNotice').textContent, /session-only/);
});

test('output renders translation and combination as separate styled text spans', () => {
  installDom();
  globalThis.KBTTrace = trace;
  const { SerialTraceApp, demoFrames } = require('../src/app.js');
  const app = new SerialTraceApp();
  app.keyboardLayout = 'fr-FR';
  for (const bytes of demoFrames()) for (const record of app.parser.push(bytes)) app.model.applyRecord(record);
  const event = app.model.events.find(event => event.kind === 'OUTPUT');
  const row = app.renderEvent(event, null);
  const main = row.children.find(child => child.className === 'eventMain');
  assert.equal(main.children[0].textContent, '{');
  assert.equal(main.children[1].className, 'keyCombination');
  assert.equal(main.children[1].textContent, " (AltGr, ')");
  assert.equal(main.children.length, 2);
  assert.match(row.title, /\{ \(AltGr, '\)/);
});

test('checkpoints default to hidden but still reconstruct and export queue state', () => {
  const { elements } = installDom();
  globalThis.KBTTrace = trace;
  const { SerialTraceApp, demoFrames } = require('../src/app.js');
  const app = new SerialTraceApp();
  app.bindUi();
  for (const bytes of demoFrames()) for (const record of app.parser.push(bytes)) app.model.applyRecord(record);
  app.render();
  assert.equal(elements.get('filterCHECKPOINT').checked, false);
  assert.equal(app.model.queueKnown, true);
  assert.equal(app.model.queue.length, 1);
  assert.equal(app.filteredEvents().some(event => event.kind === 'CHECKPOINT'), false);
  assert.equal(app.captureExport().events.filter(event => event.kind === 'CHECKPOINT').length, 3);
  assert.doesNotMatch(elements.get('details').textContent, /^CHECKPOINT/);

  elements.get('filterCHECKPOINT').onchange({ target: { checked: true } });
  assert.equal(app.filteredEvents().filter(event => event.kind === 'CHECKPOINT').length, 3);
  assert.match(elements.get('details').textContent, /^CHECKPOINT/);
  app.selectedEventId = app.model.events.at(-1).id;
  elements.get('filterCHECKPOINT').onchange({ target: { checked: false } });
  assert.equal(app.filteredEvents().some(event => event.kind === 'CHECKPOINT'), false);
  assert.doesNotMatch(elements.get('details').textContent, /^CHECKPOINT/);
  assert.equal(app.model.queueKnown, true);
});

test('DETAILS is opt-in while significant decisions and faults remain visible', () => {
  const { elements } = installDom();
  globalThis.KBTTrace = trace;
  const { SerialTraceApp, demoFrames } = require('../src/app.js');
  const app = new SerialTraceApp();
  app.bindUi();
  for (const bytes of demoFrames()) for (const record of app.parser.push(bytes)) app.model.applyRecord(record);
  for (const action of [2, 4, 9]) {
    const bytes = makeFrame(4, app.model.expectedSequence, 1300000 + action, p => {
      p.setUint32(0, 101, true);
      p.setUint8(8, action);
    });
    for (const record of app.parser.push(bytes)) app.model.applyRecord(record);
  }
  app.render();
  assert.equal(elements.get('filterDETAILS').checked, false);
  assert.equal(app.model.queueKnown, true);
  assert.equal(app.model.queue.length, 1);
  assert.equal(app.filteredEvents().some(event => event.kind === 'DETAILS'), false);
  assert.deepEqual(app.filteredEvents().filter(event => event.kind === 'INTERNAL').map(event => event.title),
    ['Queue overflow', 'Debounced key pair', 'Overlap removed', 'Layer changed']);
  assert.equal(app.captureExport().events.filter(event => event.kind === 'DETAILS').length, 3);
  elements.get('filterDETAILS').onchange({ target: { checked: true } });
  assert.equal(app.filteredEvents().filter(event => event.kind === 'DETAILS').length, 3);
  elements.get('filterDETAILS').onchange({ target: { checked: false } });
  assert.equal(app.filteredEvents().some(event => event.kind === 'DETAILS'), false);
  assert.equal(app.model.queueKnown, true);
});

test('all event categories render one summary line and keep details in the inspector', () => {
  const { elements } = installDom();
  globalThis.KBTTrace = trace;
  const { SerialTraceApp, demoFrames } = require('../src/app.js');
  const app = new SerialTraceApp();
  app.filters.add('DETAILS');
  app.filters.add('CHECKPOINT');
  for (const bytes of demoFrames()) for (const record of app.parser.push(bytes)) app.model.applyRecord(record);
  assert.equal(new Set(app.model.events.map(event => event.kind)).size, 5);
  for (const event of app.model.events) {
    const row = app.renderEvent(event, { timestampMicros: 0n });
    assert.match(row.className, /compact/);
    assert.equal(row.children.length, 3);
    const main = row.children.find(child => child.className === 'eventMain');
    assert.equal(main.children.length, event.kind === 'OUTPUT' ? 2 : 1);
    assert.equal(main.children[0].textContent, trace.eventPresentation(event).primary);
    assert.equal(row.children.at(-1).className, 'eventTime');
    app.selectedEventId = event.id;
    app.renderDetails();
    assert.ok(elements.get('details').textContent.includes(event.detail));
    assert.match(elements.get('details').textContent, /Sequence:[\s\S]*QUEUE AFTER THIS EVENT:[\s\S]*Raw:/);
  }
});
