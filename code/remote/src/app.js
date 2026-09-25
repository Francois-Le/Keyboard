(function (root) {
  'use strict';

  const trace = root.KBTTrace;
  if (!trace) throw new Error('KBTTrace must be loaded before app.js');

  const KIND_FILTERS = ['INPUT', 'INTERNAL', 'OUTPUT', 'DETAILS', 'CHECKPOINT'];
  const MAX_RENDERED_EVENTS = 300;
  const RENDER_INTERVAL_MS = 50;
  const LAYOUT_STORAGE_KEY = 'keyboard-trace.layout';

  function $(id) {
    return document.getElementById(id);
  }

  function formatMicros(value) {
    if (value === null || value === undefined) return '—';
    const n = typeof value === 'bigint' ? Number(value) : value;
    if (!Number.isFinite(n)) return String(value) + 'µs';
    if (Math.abs(n) < 1000) return `${n}µs`;
    if (Math.abs(n) < 1000000) return `${(n / 1000).toFixed(3)}ms`;
    return `${(n / 1000000).toFixed(3)}s`;
  }

  function downloadText(filename, text) {
    const blob = new Blob([text], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  function makeFrame(type, seq, micros, fillPayload) {
    const bytes = new Uint8Array(trace.FRAME_SIZE);
    bytes.set(trace.MAGIC, 0);
    bytes[4] = type;
    const view = new DataView(bytes.buffer);
    view.setUint32(8, seq, true);
    view.setBigUint64(12, BigInt(micros), true);
    const payload = new DataView(bytes.buffer, 20, trace.PAYLOAD_SIZE);
    fillPayload(payload, bytes);
    view.setUint16(46, trace.crc16CcittFalse(bytes, 0, 46), true);
    return bytes;
  }

  function demoFrames() {
    let seq = 1;
    const frames = [];
    frames.push(makeFrame(1, seq++, 1000000, p => {
      p.setUint8(0, 2); p.setUint8(1, 4); p.setUint8(2, 5); p.setUint8(3, 1);
      p.setUint32(4, 5000, true); p.setUint32(8, 35000, true); p.setUint32(12, 180000, true); p.setUint32(16, 120, true); p.setUint32(20, 4242, true);
    }));
    frames.push(makeFrame(6, seq++, 1000500, p => { p.setUint16(0, 0, true); p.setUint8(2, 0); p.setUint8(3, 0); p.setUint32(4, 1, true); }));
    frames.push(makeFrame(8, seq++, 1000510, p => { p.setUint16(0, 0, true); p.setUint32(4, 1, true); }));
    frames.push(makeFrame(2, seq++, 1004000, p => { p.setUint32(0, 100, true); p.setUint32(4, 1003900, true); p.setUint8(8, 1); p.setUint8(9, 2); p.setUint8(10, 1); p.setUint8(11, 0); }));
    frames.push(makeFrame(4, seq++, 1009000, p => { p.setUint32(0, 100, true); p.setUint32(4, 0, true); p.setUint8(8, 1); p.setUint8(9, 0); p.setUint8(10, 0); }));
    frames.push(makeFrame(4, seq++, 1043000, p => { p.setUint32(0, 100, true); p.setUint32(4, 0, true); p.setUint8(8, 8); p.setUint8(9, 1); p.setUint8(10, 0); }));
    frames.push(makeFrame(12, seq++, 1043800, (p, b) => {
      p.setUint8(0, 1); p.setUint8(1, 1); p.setUint32(2, 420, true); p.setUint32(6, 80, true);
      b.set([1, 64, 0, 33, 0, 0, 0, 0, 0, 3, 0], 30);
    }));
    frames.push(makeFrame(3, seq++, 1044200, p => { p.setUint32(0, 100, true); p.setUint8(4, 1); p.setUint8(5, 0); }));
    frames.push(makeFrame(2, seq++, 1180000, p => { p.setUint32(0, 101, true); p.setUint32(4, 1180000, true); p.setUint8(8, 3); p.setUint8(9, 1); p.setUint8(10, 1); p.setUint8(11, 1); }));
    frames.push(makeFrame(11, seq++, 1180200, p => { p.setUint8(0, 2); p.setUint8(1, 1); }));
    frames.push(makeFrame(6, seq++, 1200000, p => { p.setUint16(0, 1, true); p.setUint8(2, 1); p.setUint8(3, 2); p.setUint32(4, 2, true); }));
    frames.push(makeFrame(7, seq++, 1200000, p => { p.setUint32(0, 101, true); p.setUint32(4, 1180000, true); p.setUint8(8, 3); p.setUint8(9, 1); p.setUint8(10, 1); p.setUint8(11, 1); }));
    frames.push(makeFrame(8, seq++, 1200000, p => { p.setUint16(0, 1, true); p.setUint32(4, 2, true); }));
    return frames;
  }

  class SerialTraceApp {
    constructor() {
      this.parser = new trace.TraceParser();
      this.model = new trace.TraceModel({ maxEvents: 2000 });
      this.port = null;
      this.reader = null;
      this.readLoopPromise = null;
      this.cleanupPromise = null;
      this.lastCleanupError = null;
      this.reading = false;
      this.connecting = false;
      this.portOpened = false;
      this.sessionDone = null;
      this.seenHelloThisConnection = false;
      this.waitingTimer = null;
      this.follow = true;
      this.filters = new Set(['INPUT', 'INTERNAL', 'OUTPUT']);
      this.selectedEventId = null;
      this.zoom = 1;
      this.renderQueued = false;
      this.lastRenderAt = 0;
      this.keyboardLayout = 'en-US';
    }

    start() {
      this.restoreLayout();
      this.bindUi();
      this.render();
      this.setStatus(this.capabilityMessage(), 'idle');
    }

    bindUi() {
      $('connectBtn').addEventListener('click', () => this.connect());
      $('disconnectBtn').addEventListener('click', () => this.disconnect());
      $('demoBtn').addEventListener('click', () => this.runDemo());
      $('clearBtn').addEventListener('click', () => { this.model.clearVisible(); this.selectedEventId = null; this.render(); });
      $('exportBtn').addEventListener('click', () => downloadText(`keyboard-trace-${new Date().toISOString().replace(/[:.]/g, '-')}.json`, JSON.stringify(this.captureExport(), null, 2)));
      $('importInput').addEventListener('change', event => this.importFile(event.target.files[0]));
      $('followToggle').addEventListener('change', event => { this.follow = event.target.checked; });
      $('zoom').addEventListener('input', event => { this.zoom = Number(event.target.value); this.render(); });
      $('keyboardLayout').addEventListener('change', event => this.setKeyboardLayout(event.target.value));
      for (const kind of KIND_FILTERS) {
        const checkbox = $(`filter${kind}`);
        checkbox.checked = this.filters.has(kind);
        checkbox.addEventListener('change', event => {
          if (event.target.checked) this.filters.add(kind); else this.filters.delete(kind);
          this.render();
        });
      }
    }

    restoreLayout() {
      try {
        const saved = window.localStorage.getItem(LAYOUT_STORAGE_KEY);
        if (saved !== null) {
          if (Object.hasOwn(trace.KEYBOARD_LAYOUTS, saved)) this.keyboardLayout = saved;
          else $('layoutNotice').textContent = 'Unknown saved layout; using English.';
        }
      } catch (error) {
        $('layoutNotice').textContent = 'Layout preference is session-only.';
        $('layoutNotice').title = `Browser storage unavailable: ${error.message}`;
      }
      $('keyboardLayout').value = this.keyboardLayout;
    }

    setKeyboardLayout(layout) {
      if (!Object.hasOwn(trace.KEYBOARD_LAYOUTS, layout)) throw new Error(`Unsupported keyboard layout: ${layout}`);
      this.keyboardLayout = layout;
      $('keyboardLayout').value = layout;
      try {
        window.localStorage.setItem(LAYOUT_STORAGE_KEY, layout);
        $('layoutNotice').textContent = '';
        $('layoutNotice').title = '';
      } catch (error) {
        $('layoutNotice').textContent = 'Layout preference is session-only.';
        $('layoutNotice').title = `Could not save layout: ${error.message}`;
      }
      const scrollTop = $('timeline').scrollTop;
      this.render();
      $('timeline').scrollTop = scrollTop;
    }

    captureExport() {
      const capture = this.model.serialize();
      capture.displayLayout = this.keyboardLayout;
      capture.events = capture.events.map(event => ({ ...event, title: trace.eventTitle(event, this.keyboardLayout) }));
      return capture;
    }

    capabilityMessage() {
      if (!('serial' in navigator)) return 'Web Serial is not available in this browser. Use Chrome/Edge over HTTPS or a local file if supported.';
      if (!window.isSecureContext) return 'Web Serial requires a secure context. Open this file locally or serve it over HTTPS.';
      return 'Ready. Select a keyboard; no data leaves this page. Typed data may be sensitive.';
    }

    setStatus(message, state = 'idle') {
      $('status').textContent = message;
      $('status').className = `status ${state}`;
    }

    async connect() {
      if (this.connecting || this.reading) return;
      if (this.port) {
        this.setStatus('The previous port could not be closed. Retry Disconnect before connecting.', 'error');
        return;
      }
      if (!('serial' in navigator) || !window.isSecureContext) {
        this.setStatus(this.capabilityMessage(), 'error');
        return;
      }
      this.connecting = true;
      let finishSession;
      this.sessionDone = new Promise(resolve => { finishSession = resolve; });
      this.seenHelloThisConnection = false;
      this.parser.reset();
      this.model.beginLiveSession('New live connection');
      $('connectBtn').disabled = true;
      this.setLiveControls(false);
      this.scheduleRender();
      try {
        const port = await navigator.serial.requestPort();
        this.port = port;
        this.setStatus('Opening serial port at 115200 baud…', 'busy');
        await port.open({ baudRate: 115200, bufferSize: 4096 });
        this.portOpened = true;
        if (typeof port.setSignals === 'function') await port.setSignals({ dataTerminalReady: true });
        this.reading = true;
        $('disconnectBtn').disabled = false;
        this.setStatus('Connected. Waiting for valid KBT1 frames…', 'ok');
        this.waitingTimer = setTimeout(() => {
          if (this.reading && !this.seenHelloThisConnection) this.setStatus('Connected, but no valid HELLO received yet. Check firmware version, port, and baud rate.', 'busy');
        }, 3000);
        this.readLoopPromise = this.readLoop();
        await this.readLoopPromise;
      } catch (error) {
        if (error && error.name === 'NotFoundError') this.setStatus('Device selection cancelled.', 'idle');
        else this.setStatus(`Connection error: ${error.message || error}`, 'error');
      } finally {
        if (this.waitingTimer) clearTimeout(this.waitingTimer);
        this.waitingTimer = null;
        await this.cleanupPort();
        this.connecting = false;
        this.readLoopPromise = null;
        $('connectBtn').disabled = !!this.port;
        $('disconnectBtn').disabled = !this.port;
        this.setLiveControls(!this.port);
        finishSession();
      }
    }

    async readLoop() {
      let localReader = null;
      try {
        if (!this.port.readable) throw new Error('The selected port has no readable stream.');
        localReader = this.port.readable.getReader();
        this.reader = localReader;
        while (this.reading) {
          const { value, done } = await localReader.read();
          if (done) {
            if (this.reading) this.setStatus('Serial stream ended. Reconnect to continue.', 'idle');
            break;
          }
          if (value && value.length) this.ingest(value);
        }
      } catch (error) {
        if (this.reading) this.setStatus(`Serial read error or unplug: ${error.message || error}`, 'error');
      } finally {
        this.reading = false;
        $('disconnectBtn').disabled = true;
        if (localReader) localReader.releaseLock();
        this.reader = null;
      }
    }

    async disconnect() {
      this.reading = false;
      $('disconnectBtn').disabled = true;
      const reader = this.reader;
      if (reader) {
        try { await reader.cancel(); } catch (error) { this.setStatus(`Reader cancel failed: ${error.message || error}`, 'error'); }
      }
      if (this.connecting) await this.sessionDone;
      else await this.cleanupPort();
      $('connectBtn').disabled = !!this.port;
      $('disconnectBtn').disabled = !this.port;
      this.setLiveControls(!this.port);
      if (!this.lastCleanupError) this.setStatus('Disconnected. Existing timeline remains local.', 'idle');
    }

    async cleanupPort() {
      if (this.cleanupPromise) return this.cleanupPromise;
      this.cleanupPromise = this.doCleanupPort().finally(() => { this.cleanupPromise = null; });
      return this.cleanupPromise;
    }

    async doCleanupPort() {
      const port = this.port;
      this.lastCleanupError = null;
      this.reader = null;
      if (this.waitingTimer) clearTimeout(this.waitingTimer);
      this.waitingTimer = null;
      if (port && this.portOpened) {
        try {
          await port.close();
          this.port = null;
          this.portOpened = false;
        } catch (error) {
          this.lastCleanupError = `Port close failed: ${error.message || error}`;
          this.setStatus(this.lastCleanupError, 'error');
        }
      } else {
        this.port = null;
      }
      this.reading = false;
      $('disconnectBtn').disabled = true;
    }

    ingest(bytes) {
      const records = this.parser.push(bytes);
      let changed = false;
      for (const record of records) {
        const events = this.model.applyRecord(record);
        if (!this.seenHelloThisConnection && record.ok && record.frame.type === 1 && !this.model.validateFramePayload(record.frame)) {
          this.seenHelloThisConnection = true;
          if (this.waitingTimer) clearTimeout(this.waitingTimer);
          this.waitingTimer = null;
          this.setStatus(`Receiving KBT1 stream ${record.frame.streamId}; waiting for checkpoint to establish queue.`, 'ok');
        }
        if (this.reading && this.seenHelloThisConnection) {
          this.setStatus(this.model.queueKnown
            ? `Receiving KBT1 stream ${this.model.streamId}; ${this.model.queue.length} queued event(s).`
            : 'Receiving KBT1; queue unknown until a complete checkpoint.', this.model.queueKnown ? 'ok' : 'busy');
        }
        if (events.length) changed = true;
      }
      if (changed) this.scheduleRender();
    }

    runDemo() {
      if (this.connecting || this.reading) return;
      this.parser.reset();
      for (const frame of demoFrames()) this.ingest(frame);
      this.setStatus('Demo trace loaded. Queue remains unknown until the first checkpoint, then follows events.', 'ok');
    }

    async importFile(file) {
      if (!file) return;
      if (this.connecting || this.reading) {
        this.setStatus('Import is disabled while a live serial connection is active.', 'error');
        $('importInput').value = '';
        return;
      }
      if (file.size > trace.MAX_IMPORT_BYTES) {
        this.setStatus(`Import failed: file is larger than ${Math.round(trace.MAX_IMPORT_BYTES / 1024 / 1024)} MiB.`, 'error');
        $('importInput').value = '';
        return;
      }
      try {
        const text = await file.text();
        this.model.importState(JSON.parse(text));
        this.parser.reset();
        this.selectedEventId = null;
        this.setStatus(`Imported ${this.model.events.length} event(s). Live parser buffer reset; model state preserved from export.`, 'ok');
        this.render();
      } catch (error) {
        this.setStatus(`Import failed: ${error.message || error}`, 'error');
      } finally {
        $('importInput').value = '';
      }
    }

    scheduleRender() {
      if (this.renderQueued) return;
      this.renderQueued = true;
      const delay = Math.max(0, RENDER_INTERVAL_MS - (Date.now() - this.lastRenderAt));
      setTimeout(() => requestAnimationFrame(() => {
        this.renderQueued = false;
        this.lastRenderAt = Date.now();
        this.render();
      }), delay);
    }

    filteredEvents() {
      return this.model.events.filter(e => this.filters.has(e.kind)).slice(-MAX_RENDERED_EVENTS);
    }

    render() {
      const events = this.filteredEvents();
      $('eventCount').textContent = `${this.model.events.length} retained / ${events.length} shown`;
      $('queueState').textContent = this.model.queueKnown ? `${this.model.queue.length} queued` : 'queue unknown';
      $('streamInfo').textContent = this.model.hello ? `stream ${this.model.hello.streamId}, ${this.model.hello.rows}x${this.model.hello.cols}, board v${this.model.hello.boardVersion}` : 'no HELLO yet';
      const timeline = $('timeline');
      timeline.innerHTML = '';
      if (!events.length) {
        const empty = document.createElement('div');
        empty.className = 'empty';
        empty.textContent = 'No visible events yet. Connect a keyboard or run Demo.';
        timeline.appendChild(empty);
      } else {
        let previousVisible = null;
        for (const event of events) {
          timeline.appendChild(this.renderEvent(event, previousVisible));
          previousVisible = event;
        }
      }
      this.renderDetails();
      if (this.follow) timeline.scrollTop = timeline.scrollHeight;
    }

    renderEvent(event, previousVisible) {
      const visibleDeltaUs = previousVisible && previousVisible.timestampMicros !== null && event.timestampMicros !== null && event.timestampMicros >= previousVisible.timestampMicros
        ? Number(event.timestampMicros - previousVisible.timestampMicros)
        : event.deltaUs;
      const deltaMs = visibleDeltaUs === null ? 0 : visibleDeltaUs / 1000;
      const desiredGap = Math.round(deltaMs * this.zoom * 0.08);
      const gap = Math.max(6, Math.min(110, desiredGap));
      const row = document.createElement('button');
      row.className = `event ${event.kind.toLowerCase()} compact ${event.severity} ${event.id === this.selectedEventId ? 'selected' : ''}`;
      row.type = 'button';
      row.style.marginTop = `${gap}px`;
      row.addEventListener('click', () => {
        this.selectedEventId = event.id;
        this.follow = false;
        $('followToggle').checked = false;
        this.render();
      });

      const lane = document.createElement('span');
      lane.className = 'lane';
      lane.textContent = event.kind;
      const main = document.createElement('span');
      main.className = 'eventMain';
      const title = document.createElement('strong');
      const presentation = trace.eventPresentation(event, this.keyboardLayout);
      const displayTitle = trace.eventTitle(event, this.keyboardLayout);
      title.textContent = presentation.primary;
      main.append(title);
      if (presentation.combination) {
        const combination = document.createElement('span');
        combination.className = 'keyCombination';
        combination.textContent = ` ${presentation.combination}`;
        main.append(combination);
      }
      const time = document.createElement('small');
      time.className = 'eventTime';
      time.textContent = formatMicros(event.timestampMicros);
      row.title = `${displayTitle}\nseq ${event.sequence ?? '—'}, visible delta ${formatMicros(visibleDeltaUs)}${desiredGap > 110 ? ' (gap compressed)' : ''}\nClick for full details`;
      row.append(lane, main, time);
      return row;
    }

    renderDetails() {
      const visible = this.filteredEvents();
      const selected = visible.find(e => e.id === this.selectedEventId) || visible[visible.length - 1];
      const panel = $('details');
      if (!selected) {
        panel.textContent = 'Click an event to inspect queue after it.';
        return;
      }
      const queue = selected.queueKnown
        ? (selected.queueAfter.length ? selected.queueAfter.map(q => `slot ${q.slot}: #${q.id} row ${q.row + 1} column ${q.col + 1} ${q.pressed ? 'down' : 'up'}`).join('\n') : '(empty)')
        : 'Unknown until a complete SNAPSHOT_BEGIN/ENTRY*/SNAPSHOT_END checkpoint.';
      panel.textContent = `${selected.kind} · ${trace.eventTitle(selected, this.keyboardLayout)}
${selected.detail}

Display layout: ${trace.KEYBOARD_LAYOUTS[this.keyboardLayout]}
Character preview assumes Caps Lock off; dead-key/IME composition and host receipt are not inferred.
Sequence: ${selected.sequence ?? '—'}
Timestamp: ${formatMicros(selected.timestampMicros)}
Delta: ${formatMicros(selected.deltaUs)}

QUEUE AFTER THIS EVENT:
${queue}

Raw:
${JSON.stringify(selected.raw, null, 2)}`;
    }

    setLiveControls(enabled) {
      $('demoBtn').disabled = !enabled;
      $('importInput').disabled = !enabled;
      const label = $('importInput').closest ? $('importInput').closest('label') : null;
      if (label) label.classList.toggle('disabled', !enabled);
    }
  }

  root.SerialTraceApp = SerialTraceApp;
  if (typeof module === 'object' && module.exports) module.exports = { SerialTraceApp, demoFrames, formatMicros };
  if (typeof window !== 'undefined') window.addEventListener('DOMContentLoaded', () => new SerialTraceApp().start());
})(typeof globalThis !== 'undefined' ? globalThis : this);
