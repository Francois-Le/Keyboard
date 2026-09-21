(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.KBTTrace = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const FRAME_SIZE = 48;
  const PAYLOAD_SIZE = 26;
  const MAGIC = [0x4b, 0x42, 0x54, 0x31]; // KBT1
  const TYPES = {
    1: 'HELLO',
    2: 'INPUT',
    3: 'REMOVE',
    4: 'DECISION',
    5: 'HID',
    6: 'SNAPSHOT_BEGIN',
    7: 'SNAPSHOT_ENTRY',
    8: 'SNAPSHOT_END',
    9: 'LOSS',
    10: 'I2C_RESET',
    11: 'QUEUE_OVERFLOW'
  };
  const INPUT = 'INPUT';
  const INTERNAL = 'INTERNAL';
  const OUTPUT = 'OUTPUT';
  const REMOVE_REASONS = {
    1: 'processed',
    2: 'debounce',
    3: 'tap',
    4: 'overlap'
  };
  const ACTIONS = {
    1: 'debounce wait',
    2: 'debounce cancel',
    3: 'overlap wait',
    4: 'overlap tap',
    5: 'tap wait',
    6: 'tap',
    7: 'hold (no tap)',
    8: 'process',
    9: 'layer'
  };
  const MAX_QUEUE_ENTRIES = 255;
  const MAX_ISSUES = 200;
  const MAX_IMPORT_BYTES = 5 * 1024 * 1024;
  const KEYBOARD_MODIFIERS = ['LCTRL', 'LSHIFT', 'LALT', 'LGUI', 'RCTRL', 'RSHIFT', 'RALT', 'RGUI'];
  const MEDIA_BITS = ['PLAY_PAUSE', 'SCAN_NEXT', 'SCAN_PREV', 'STOP', 'MUTE', 'VOLUME_UP', 'VOLUME_DOWN', 'EJECT'];

  function crc16CcittFalse(bytes, start = 0, end = bytes.length) {
    let crc = 0xffff;
    for (let i = start; i < end; i++) {
      crc ^= bytes[i] << 8;
      for (let bit = 0; bit < 8; bit++) {
        crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
        crc &= 0xffff;
      }
    }
    return crc;
  }

  function concatBytes(a, b) {
    if (!a || a.length === 0) return new Uint8Array(b);
    const out = new Uint8Array(a.length + b.length);
    out.set(a, 0);
    out.set(b, a.length);
    return out;
  }

  function bytesEqualMagic(bytes, offset) {
    return bytes[offset] === MAGIC[0] &&
      bytes[offset + 1] === MAGIC[1] &&
      bytes[offset + 2] === MAGIC[2] &&
      bytes[offset + 3] === MAGIC[3];
  }

  function findMagic(bytes, start) {
    for (let i = start; i <= bytes.length - 4; i++) {
      if (bytesEqualMagic(bytes, i)) return i;
    }
    return -1;
  }

  function findProtocolPrefix(bytes, start) {
    for (let i = start; i <= bytes.length - 3; i++) {
      if (bytes[i] === MAGIC[0] && bytes[i + 1] === MAGIC[1] && bytes[i + 2] === MAGIC[2]) return i;
    }
    return -1;
  }

  function readU32(view, offset) {
    return view.getUint32(offset, true);
  }

  function readU64(view, offset) {
    if (typeof view.getBigUint64 === 'function') return view.getBigUint64(offset, true);
    const lo = BigInt(view.getUint32(offset, true));
    const hi = BigInt(view.getUint32(offset + 4, true));
    return (hi << 32n) | lo;
  }

  function clonePayload(bytes) {
    return Array.from(bytes.slice(20, 46));
  }

  class TraceParser {
    constructor(options = {}) {
      this.buffer = new Uint8Array(0);
      this.maxBufferedGarbage = options.maxBufferedGarbage || 192;
    }

    reset() {
      this.buffer = new Uint8Array(0);
    }

    push(chunk) {
      const input = chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk);
      this.buffer = concatBytes(this.buffer, input);
      const records = [];

      while (this.buffer.length >= 4) {
        const protocolAt = findProtocolPrefix(this.buffer, 0);
        if (protocolAt >= 0 && this.buffer.length >= protocolAt + 4 && this.buffer[protocolAt + 3] !== MAGIC[3]) {
          if (protocolAt > 0) records.push({ ok: false, error: `Discarded ${protocolAt} byte(s) before KBT protocol marker.` });
          records.push({ ok: false, error: `Unsupported KBT protocol version byte 0x${this.buffer[protocolAt + 3].toString(16).padStart(2, '0')}; this app supports KBT1 only.` });
          this.buffer = this.buffer.slice(protocolAt + 4);
          continue;
        }
        const magicAt = findMagic(this.buffer, 0);
        if (magicAt < 0) {
          const keep = Math.min(3, this.buffer.length);
          const dropped = this.buffer.length - keep;
          if (dropped > 0) records.push({ ok: false, error: `Discarded ${dropped} byte(s) while searching for KBT1 frame magic.` });
          this.buffer = this.buffer.slice(this.buffer.length - keep);
          break;
        }
        if (magicAt > 0) {
          records.push({ ok: false, error: `Discarded ${magicAt} byte(s) before KBT1 frame magic.` });
          this.buffer = this.buffer.slice(magicAt);
        }
        if (this.buffer.length < FRAME_SIZE) break;

        const frameBytes = this.buffer.slice(0, FRAME_SIZE);
        const view = new DataView(frameBytes.buffer, frameBytes.byteOffset, frameBytes.byteLength);
        const expectedCrc = view.getUint16(46, true);
        const actualCrc = crc16CcittFalse(frameBytes, 0, 46);
        if (expectedCrc !== actualCrc) {
          records.push({ ok: false, error: `CRC mismatch at sequence candidate ${view.getUint32(8, true)} (expected 0x${expectedCrc.toString(16).padStart(4, '0')}, got 0x${actualCrc.toString(16).padStart(4, '0')}). Resynchronizing.` });
          this.buffer = this.buffer.slice(1);
          continue;
        }

        const type = frameBytes[4];
        if (frameBytes[5] !== 0 || frameBytes[6] !== 0 || frameBytes[7] !== 0) {
          records.push({ ok: false, error: `Reserved header bytes are non-zero for sequence ${view.getUint32(8, true)}. Frame ignored.` });
          this.buffer = this.buffer.slice(FRAME_SIZE);
          continue;
        }
        if (!TYPES[type]) {
          records.push({ ok: false, error: `Unsupported KBT1 frame type ${type} at sequence ${view.getUint32(8, true)}. Firmware/app protocol versions may not match.` });
          this.buffer = this.buffer.slice(FRAME_SIZE);
          continue;
        }

        records.push({ ok: true, frame: parseFrame(frameBytes, view) });
        this.buffer = this.buffer.slice(FRAME_SIZE);
        if (this.buffer.length > this.maxBufferedGarbage && findMagic(this.buffer, 0) < 0) {
          records.push({ ok: false, error: `Discarded ${this.buffer.length - 3} buffered garbage byte(s).` });
          this.buffer = this.buffer.slice(-3);
        }
      }
      return records;
    }
  }

  function parseFrame(bytes, view) {
    const type = bytes[4];
    const payload = new DataView(bytes.buffer, bytes.byteOffset + 20, PAYLOAD_SIZE);
    const frame = {
      type,
      typeName: TYPES[type],
      sequence: readU32(view, 8),
      timestampMicros: readU64(view, 12),
      payload: clonePayload(bytes)
    };

    switch (type) {
      case 1:
        frame.boardVersion = payload.getUint8(0);
        frame.rows = payload.getUint8(1);
        frame.cols = payload.getUint8(2);
        frame.overlapRaw = payload.getUint8(3);
        frame.overlapEnabled = frame.overlapRaw !== 0;
        frame.debounceUs = readU32(payload, 4);
        frame.overlapUs = readU32(payload, 8);
        frame.maxHoldUs = readU32(payload, 12);
        frame.keyPressMs = readU32(payload, 16);
        frame.streamId = readU32(payload, 20);
        break;
      case 2:
      case 7:
        frame.id = readU32(payload, 0);
        frame.sampleMicros32 = readU32(payload, 4);
        frame.row = payload.getUint8(8);
        frame.col = payload.getUint8(9);
        frame.pressedRaw = payload.getUint8(10);
        frame.pressed = frame.pressedRaw !== 0;
        frame.slot = payload.getUint8(11);
        break;
      case 3:
        frame.id = readU32(payload, 0);
        frame.reason = payload.getUint8(4);
        frame.slot = payload.getUint8(5);
        break;
      case 4:
        frame.primaryID = readU32(payload, 0);
        frame.relatedID = readU32(payload, 4);
        frame.action = payload.getUint8(8);
        frame.layerMask = payload.getUint8(9);
        frame.interveningCount = payload.getUint8(10);
        break;
      case 5:
        frame.reportLength = payload.getUint8(0);
        frame.successRaw = payload.getUint8(1);
        frame.success = frame.successRaw !== 0;
        frame.sendDurationUs = readU32(payload, 2);
        frame.report = Array.from(bytes.slice(26, 46)).slice(0, frame.reportLength);
        break;
      case 6:
        frame.count = payload.getUint16(0, true);
        frame.head = payload.getUint8(2);
        frame.tail = payload.getUint8(3);
        frame.checkpointID = readU32(payload, 4);
        break;
      case 8:
        frame.count = payload.getUint16(0, true);
        frame.checkpointID = readU32(payload, 4);
        break;
      case 9:
        frame.totalDropped = readU32(payload, 0);
        break;
      case 10:
        frame.chipIndex = payload.getUint8(0);
        break;
      case 11:
        frame.head = payload.getUint8(0);
        frame.tail = payload.getUint8(1);
        break;
    }
    return frame;
  }

  function jsonSafe(value) {
    if (typeof value === 'bigint') return value.toString();
    if (Array.isArray(value)) return value.map(jsonSafe);
    if (value && typeof value === 'object') {
      const out = {};
      for (const [k, v] of Object.entries(value)) out[k] = jsonSafe(v);
      return out;
    }
    return value;
  }

  class TraceModel {
    constructor(options = {}) {
      this.maxEvents = options.maxEvents || 2000;
      this.resetAll();
    }

    resetAll() {
      this.events = [];
      this.queue = [];
      this.queueKnown = false;
      this.hello = null;
      this.streamId = null;
      this.expectedSequence = null;
      this.pendingSnapshot = null;
      this.nextEventId = 1;
      this.lastVisibleTimestamp = null;
      this.issues = [];
      this.lastHidReports = new Map();
      this.lastFrameTimestamp = null;
    }

    clearVisible() {
      this.events = [];
      this.lastVisibleTimestamp = null;
    }

    beginLiveSession(label = 'Live connection boundary') {
      this.queueKnown = false;
      this.pendingSnapshot = null;
      this.expectedSequence = null;
      this.hello = null;
      this.streamId = null;
      this.lastHidReports = new Map();
      this.lastFrameTimestamp = null;
      return this.addEvent(INTERNAL, label, 'Parser/model live state reset for a new serial connection. Earlier visible events are preserved, but queue is unknown until the next complete checkpoint.', { timestampMicros: this.lastVisibleTimestamp, sequence: null, typeName: 'LOCAL' }, { severity: 'warn' });
    }

    applyRecord(record) {
      if (!record.ok) {
        return [this.addIssue(record.error, null)];
      }
      return this.applyFrame(record.frame);
    }

    applyFrame(frame) {
      const generated = [];
      if (this.lastFrameTimestamp !== null && frame.timestampMicros < this.lastFrameTimestamp) {
        this.queueKnown = false;
        this.pendingSnapshot = null;
        this.lastHidReports.clear();
        generated.push(this.addEvent(INTERNAL, 'Device clock reset', 'A new device clock epoch started; queue and HID baselines are unknown.', frame, { severity: 'warn' }));
      }
      this.lastFrameTimestamp = frame.timestampMicros;
      if (this.expectedSequence !== null && frame.sequence !== this.expectedSequence) {
        this.queueKnown = false;
        this.pendingSnapshot = null;
        this.lastHidReports.clear();
        generated.push(this.addEvent(INTERNAL, 'Sequence gap', `Expected ${this.expectedSequence}, received ${frame.sequence}. Queue state is unknown until a complete checkpoint.`, frame, { severity: 'warn' }));
      }
      this.expectedSequence = (frame.sequence + 1) >>> 0;

      const malformed = this.validateFramePayload(frame);
      if (malformed) {
        generated.push(this.addIssue(malformed, frame));
        return generated;
      }

      if (this.pendingSnapshot && frame.type !== 7 && frame.type !== 8) {
        this.pendingSnapshot = null;
        this.queueKnown = false;
        generated.push(this.addEvent(INTERNAL, 'Checkpoint interrupted', `${frame.typeName} arrived before SNAPSHOT_END; queue state remains unknown until a complete uninterrupted checkpoint.`, frame, { severity: 'warn' }));
      }

      switch (frame.type) {
        case 1:
          generated.push(this.handleHello(frame));
          break;
        case 2:
          generated.push(this.handleInput(frame));
          break;
        case 3:
          generated.push(this.handleRemove(frame));
          break;
        case 4:
          generated.push(this.handleDecision(frame));
          break;
        case 5:
          generated.push(this.handleHid(frame));
          break;
        case 6:
          generated.push(this.handleSnapshotBegin(frame));
          break;
        case 7:
          generated.push(this.handleSnapshotEntry(frame));
          break;
        case 8:
          generated.push(this.handleSnapshotEnd(frame));
          break;
        case 9:
          this.queueKnown = false;
          this.pendingSnapshot = null;
          this.lastHidReports.clear();
          generated.push(this.addEvent(INTERNAL, 'Trace frame loss', `${frame.totalDropped} dropped trace frame(s) reported by firmware in this stream. Queue state is unknown until checkpoint.`, frame, { severity: 'warn' }));
          break;
        case 10:
          generated.push(this.addEvent(INTERNAL, 'I2C reset', `MCP23008 chip ${frame.chipIndex} reset.`, frame));
          break;
        case 11:
          this.queueKnown = false;
          this.pendingSnapshot = null;
          generated.push(this.addEvent(INTERNAL, 'Queue overflow', `Observer queue overflow at head ${frame.head}, tail ${frame.tail}; queue state unknown until checkpoint.`, frame, { severity: 'warn' }));
          break;
      }
      return generated.filter(Boolean);
    }

    handleHello(frame) {
      const changedStream = this.streamId !== null && this.streamId !== frame.streamId;
      this.hello = {
        boardVersion: frame.boardVersion,
        rows: frame.rows,
        cols: frame.cols,
        overlapEnabled: frame.overlapEnabled,
        debounceUs: frame.debounceUs,
        overlapUs: frame.overlapUs,
        maxHoldUs: frame.maxHoldUs,
        keyPressMs: frame.keyPressMs,
        streamId: frame.streamId
      };
      this.streamId = frame.streamId;
      if (changedStream) {
        this.queueKnown = false;
        this.pendingSnapshot = null;
        this.lastHidReports.clear();
      }
      const title = changedStream ? 'New trace stream' : 'HELLO checkpoint';
      const detail = `board v${frame.boardVersion}, ${frame.rows}x${frame.cols}, stream ${frame.streamId}, debounce ${frame.debounceUs}µs, overlap ${frame.overlapEnabled ? frame.overlapUs + 'µs' : 'off'}. HELLO does not establish queue contents.`;
      return this.addEvent(INTERNAL, title, detail, frame, { severity: changedStream ? 'info' : 'muted' });
    }

    handleInput(frame) {
      const problem = this.validateKey(frame);
      if (problem) return this.addIssue(`Invalid input frame: ${problem}`, frame);
      const item = {
        id: frame.id,
        row: frame.row,
        col: frame.col,
        pressed: frame.pressed,
        slot: frame.slot,
        sampleMicros32: frame.sampleMicros32
      };
      if (this.queueKnown) {
        if (this.queue.length === MAX_QUEUE_ENTRIES) return this.addIssue('INPUT exceeds processing queue capacity; queue invalidated.', frame);
        const conflict = this.queue.find(q => q.id === item.id || q.slot === item.slot);
        if (conflict) {
          return this.addIssue(`INPUT id ${item.id}/slot ${item.slot} duplicates queued id ${conflict.id}/slot ${conflict.slot}; queue invalidated.`, frame);
        }
        this.queue.push(item);
      }
      return this.addEvent(INPUT, frame.pressed ? 'Key press enqueued' : 'Key release enqueued', `id ${frame.id}, row ${frame.row}, col ${frame.col}, slot ${frame.slot}, sampled ${frame.sampleMicros32}µs.`, frame);
    }

    handleRemove(frame) {
      const reason = REMOVE_REASONS[frame.reason] || `unknown(${frame.reason})`;
      if (!REMOVE_REASONS[frame.reason]) return this.addIssue(`Unknown REMOVE reason ${frame.reason}; queue invalidated.`, frame);
      if (this.queueKnown) {
        const index = this.queue.findIndex(q => q.id === frame.id && q.slot === frame.slot);
        if (index < 0) {
          const near = this.queue.find(q => q.id === frame.id || q.slot === frame.slot);
          return this.addIssue(`REMOVE id ${frame.id}/slot ${frame.slot} did not match a queued entry${near ? ` (found id ${near.id}/slot ${near.slot})` : ''}; queue invalidated.`, frame);
        }
        this.queue.splice(index, 1);
      }
      return this.addEvent(INTERNAL, 'Queue remove', `id ${frame.id}, slot ${frame.slot}, reason ${reason}.`, frame, { severity: REMOVE_REASONS[frame.reason] ? 'info' : 'warn' });
    }

    handleDecision(frame) {
      const action = ACTIONS[frame.action] || `unknown(${frame.action})`;
      if (!ACTIONS[frame.action]) {
        return this.addEvent(INTERNAL, 'Invalid decision', `Unknown action ${frame.action} for primary ${frame.primaryID}.`, frame, { severity: 'error' });
      }
      return this.addEvent(INTERNAL, 'Decision: ' + action, `primary ${frame.primaryID}, related ${frame.relatedID}, layer mask 0x${frame.layerMask.toString(16)}, intervening ${frame.interveningCount}.`, frame);
    }

    handleHid(frame) {
      const validLength = (frame.reportLength === 9 && frame.report[0] === 1) || (frame.reportLength === 2 && frame.report[0] === 3);
      const bytes = frame.report.map(b => b.toString(16).padStart(2, '0')).join(' ');
      const interpreted = validLength ? this.describeHid(frame) : 'Invalid report ID/length (keyboard requires ID 1 length 9; media requires ID 3 length 2).';
      return this.addEvent(OUTPUT, frame.success ? 'HID send submitted' : 'HID send failed', `${interpreted} length ${frame.reportLength}, duration ${frame.sendDurationUs}µs, bytes [${bytes}]. API submission does not prove host receipt.`, frame, { severity: frame.success && validLength ? 'info' : 'warn' });
    }

    handleSnapshotBegin(frame) {
      if (frame.count > MAX_QUEUE_ENTRIES) {
        return this.addIssue(`SNAPSHOT_BEGIN count ${frame.count} exceeds ${MAX_QUEUE_ENTRIES}; queue invalidated.`, frame);
      }
      this.pendingSnapshot = {
        count: frame.count,
        head: frame.head,
        tail: frame.tail,
        checkpointID: frame.checkpointID,
        entries: [],
        ids: new Set(),
        slots: new Set(),
        timestampMicros: frame.timestampMicros,
        sequence: frame.sequence
      };
      return null;
    }

    handleSnapshotEntry(frame) {
      if (!this.pendingSnapshot) {
        this.queueKnown = false;
        return this.addIssue(`Snapshot entry ${frame.id} arrived without a matching begin; queue remains unknown.`, frame);
      }
      const problem = this.validateKey(frame);
      if (problem) {
        return this.addIssue(`Invalid snapshot entry: ${problem}`, frame);
      }
      if (this.pendingSnapshot.entries.length >= this.pendingSnapshot.count || this.pendingSnapshot.entries.length >= MAX_QUEUE_ENTRIES) {
        return this.addIssue(`Snapshot has more entries than declared count ${this.pendingSnapshot.count}; queue invalidated.`, frame);
      }
      if (this.pendingSnapshot.ids.has(frame.id) || this.pendingSnapshot.slots.has(frame.slot)) {
        return this.addIssue(`Snapshot duplicate id/slot (${frame.id}/${frame.slot}); queue invalidated.`, frame);
      }
      this.pendingSnapshot.ids.add(frame.id);
      this.pendingSnapshot.slots.add(frame.slot);
      this.pendingSnapshot.entries.push({
        id: frame.id,
        row: frame.row,
        col: frame.col,
        pressed: frame.pressed,
        slot: frame.slot,
        sampleMicros32: frame.sampleMicros32
      });
    }

    handleSnapshotEnd(frame) {
      if (!this.pendingSnapshot) {
        this.queueKnown = false;
        return this.addEvent(INTERNAL, 'Checkpoint rejected', `SNAPSHOT_END ${frame.checkpointID} arrived without a valid uninterrupted begin.`, frame, { severity: 'warn' });
      }
      const snap = this.pendingSnapshot;
      this.pendingSnapshot = null;
      if (snap.checkpointID !== frame.checkpointID || snap.count !== frame.count || snap.entries.length !== frame.count) {
        this.queueKnown = false;
        return this.addEvent(INTERNAL, 'Checkpoint rejected', `Expected checkpoint ${snap.checkpointID} with ${snap.count} entries, got checkpoint ${frame.checkpointID} with ${frame.count} end count and ${snap.entries.length} entries.`, frame, { severity: 'warn' });
      }
      const ringProblem = this.validateSnapshotRing(snap);
      if (ringProblem) return this.addIssue(ringProblem, frame);
      this.queue = snap.entries.slice();
      this.queueKnown = true;
      return this.addEvent(INTERNAL, 'Queue checkpoint', `Checkpoint ${frame.checkpointID}: ${frame.count} queued event(s), head ${snap.head}, tail ${snap.tail}.`, frame, { severity: 'checkpoint' });
    }

    validateKey(frame) {
      if (!this.hello) return null;
      if (frame.row >= this.hello.rows || frame.col >= this.hello.cols) {
        this.queueKnown = false;
        return `Coordinate row ${frame.row}, col ${frame.col} is outside HELLO dimensions ${this.hello.rows}x${this.hello.cols}; queue state invalidated.`;
      }
      return null;
    }

    validateFramePayload(frame) {
      if (frame.type === 1) {
        if (frame.rows < 1 || frame.rows > 32 || frame.cols < 1 || frame.cols > 32) return `Invalid HELLO dimensions ${frame.rows}x${frame.cols}.`;
        if (frame.overlapRaw !== 0 && frame.overlapRaw !== 1) return `Invalid HELLO overlapEnabled value ${frame.overlapRaw}.`;
      }
      if (frame.type === 2 || frame.type === 7) {
        if (frame.pressedRaw !== 0 && frame.pressedRaw !== 1) return `Invalid ${frame.typeName} pressed value ${frame.pressedRaw}.`;
      }
      if (frame.type === 3 && !REMOVE_REASONS[frame.reason]) return `Invalid REMOVE reason ${frame.reason}.`;
      if (frame.type === 4 && !ACTIONS[frame.action]) return `Invalid DECISION action ${frame.action}.`;
      if (frame.type === 5) {
        if (frame.successRaw !== 0 && frame.successRaw !== 1) return `Invalid HID success value ${frame.successRaw}.`;
        const validKeyboard = frame.reportLength === 9 && frame.report[0] === 1;
        const validMedia = frame.reportLength === 2 && frame.report[0] === 3;
        if (!validKeyboard && !validMedia) return `Invalid HID report ID/length: id ${frame.report[0] ?? 'none'}, length ${frame.reportLength}.`;
      }
      if ((frame.type === 6 || frame.type === 8) && frame.count > MAX_QUEUE_ENTRIES) return `${frame.typeName} count ${frame.count} exceeds ${MAX_QUEUE_ENTRIES}.`;
      return null;
    }

    validateSnapshotRing(snap) {
      if (snap.count === 0) {
        return snap.head === snap.tail ? null : `Empty checkpoint has head ${snap.head} but tail ${snap.tail}; queue invalidated.`;
      }
      const span = (snap.tail - snap.head + 256) & 0xff;
      if (snap.count > span) return 'Checkpoint contains more live entries than physical ring slots.';
      let previousDistance = -1;
      for (let i = 0; i < snap.entries.length; i++) {
        // Removed entries remain as tombstones, so live slots need not be adjacent.
        const distance = (snap.entries[i].slot - snap.head + 256) & 0xff;
        if ((i === 0 && distance !== 0) || distance <= previousDistance || distance >= span) {
          return `Checkpoint entry ${i} has slot ${snap.entries[i].slot} outside ordered head/tail range.`;
        }
        previousDistance = distance;
      }
      return null;
    }

    describeHid(frame) {
      const id = frame.report[0];
      const key = String(id);
      const previous = this.lastHidReports.get(key);
      if (frame.success) this.lastHidReports.set(key, frame.report.slice());
      else this.lastHidReports.delete(key);
      if (id === 1) {
        const modifiers = KEYBOARD_MODIFIERS.filter((_, bit) => (frame.report[1] & (1 << bit)) !== 0);
        const keys = frame.report.slice(3, 9).filter(Boolean);
        const prefix = previous ? this.describeKeyboardDiff(previous, frame.report) : 'keyboard baseline';
        return `${prefix}: modifiers [${modifiers.join(', ') || 'none'}], keys [${keys.map(k => '0x' + k.toString(16).padStart(2, '0')).join(', ') || 'none'}].`;
      }
      const bits = frame.report[1];
      const active = MEDIA_BITS.filter((_, bit) => (bits & (1 << bit)) !== 0);
      const prefix = previous ? this.describeMediaDiff(previous, frame.report) : 'media baseline';
      return `${prefix}: media [${active.join(', ') || 'none'}].`;
    }

    describeKeyboardDiff(previous, current) {
      const oldMods = previous[1] || 0;
      const newMods = current[1] || 0;
      const pressedMods = KEYBOARD_MODIFIERS.filter((_, bit) => !(oldMods & (1 << bit)) && (newMods & (1 << bit)));
      const releasedMods = KEYBOARD_MODIFIERS.filter((_, bit) => (oldMods & (1 << bit)) && !(newMods & (1 << bit)));
      const oldKeys = new Set(previous.slice(3, 9).filter(Boolean));
      const newKeys = new Set(current.slice(3, 9).filter(Boolean));
      const pressed = [...newKeys].filter(k => !oldKeys.has(k)).map(k => '0x' + k.toString(16).padStart(2, '0'));
      const released = [...oldKeys].filter(k => !newKeys.has(k)).map(k => '0x' + k.toString(16).padStart(2, '0'));
      const parts = [];
      if (pressed.length || pressedMods.length) parts.push(`pressed ${[...pressedMods, ...pressed].join(', ')}`);
      if (released.length || releasedMods.length) parts.push(`released ${[...releasedMods, ...released].join(', ')}`);
      return parts.join('; ') || 'keyboard unchanged';
    }

    describeMediaDiff(previous, current) {
      const oldBits = previous[1] || 0;
      const newBits = current[1] || 0;
      const pressed = MEDIA_BITS.filter((_, bit) => !(oldBits & (1 << bit)) && (newBits & (1 << bit)));
      const released = MEDIA_BITS.filter((_, bit) => (oldBits & (1 << bit)) && !(newBits & (1 << bit)));
      const parts = [];
      if (pressed.length) parts.push(`pressed ${pressed.join(', ')}`);
      if (released.length) parts.push(`released ${released.join(', ')}`);
      return parts.join('; ') || 'media unchanged';
    }

    addIssue(message, frame) {
      this.queueKnown = false;
      this.pendingSnapshot = null;
      this.lastHidReports.clear();
      this.issues.push({ message, sequence: frame ? frame.sequence : null });
      if (this.issues.length > MAX_ISSUES) this.issues.splice(0, this.issues.length - MAX_ISSUES);
      return this.addEvent(INTERNAL, 'Parser/protocol issue', message, frame || { timestampMicros: this.lastVisibleTimestamp, sequence: null }, { severity: 'error' });
    }

    addEvent(kind, title, detail, frame, options = {}) {
      const ts = frame && frame.timestampMicros !== undefined && frame.timestampMicros !== null ? BigInt(frame.timestampMicros) : this.lastVisibleTimestamp;
      let deltaUs = null;
      if (ts !== null && this.lastVisibleTimestamp !== null && ts >= this.lastVisibleTimestamp) {
        deltaUs = Number(ts - this.lastVisibleTimestamp);
      }
      if (ts !== null) this.lastVisibleTimestamp = ts;
      const event = {
        id: this.nextEventId++,
        kind,
        title,
        detail,
        sequence: frame ? frame.sequence : null,
        typeName: frame && frame.typeName ? frame.typeName : 'LOCAL',
        timestampMicros: ts,
        deltaUs,
        severity: options.severity || 'info',
        queueKnown: this.queueKnown,
        queueAfter: this.queueKnown ? this.queue.map(q => ({ ...q })) : null,
        raw: frame ? jsonSafe(frame) : null
      };
      this.events.push(event);
      if (this.events.length > this.maxEvents) this.events.splice(0, this.events.length - this.maxEvents);
      return event;
    }

    serialize() {
      return {
        format: 'KBT_TRACE_MODEL_V1',
        exportedAt: new Date().toISOString(),
        maxEvents: this.maxEvents,
        queueKnown: this.queueKnown,
        queue: this.queue,
        hello: this.hello,
        streamId: this.streamId,
        expectedSequence: this.expectedSequence,
        nextEventId: this.nextEventId,
        events: jsonSafe(this.events),
        issues: this.issues
      };
    }

    importState(state) {
      const next = this.validateImportState(state);
      this.queueKnown = next.queueKnown;
      this.queue = next.queue;
      this.hello = next.hello;
      this.streamId = next.streamId;
      this.expectedSequence = next.expectedSequence;
      this.nextEventId = next.nextEventId;
      this.issues = next.issues;
      this.events = next.events;
      this.pendingSnapshot = null;
      this.lastHidReports = new Map();
      this.lastVisibleTimestamp = this.events.length ? this.events[this.events.length - 1].timestampMicros : null;
    }

    validateImportState(state) {
      if (!state || state.format !== 'KBT_TRACE_MODEL_V1' || !Array.isArray(state.events)) throw new Error('Not a KBT trace export.');
      const cleanQueue = this.validateQueueArray(state.queue || [], 'queue');
      const cleanEvents = state.events.slice(-this.maxEvents).map((e, index) => this.validateEvent(e, index));
      const maxId = cleanEvents.reduce((m, e) => Math.max(m, e.id), 0);
      return {
        queueKnown: !!state.queueKnown,
        queue: cleanQueue,
        hello: state.hello === null || state.hello === undefined ? null : this.validateHelloImport(state.hello),
        streamId: safeOptionalUint32(state.streamId, 'streamId'),
        expectedSequence: safeOptionalUint32(state.expectedSequence, 'expectedSequence'),
        nextEventId: Math.max(safeOptionalPositiveInt(state.nextEventId, 'nextEventId') || 1, maxId + 1),
        issues: Array.isArray(state.issues) ? state.issues.slice(-MAX_ISSUES).map((issue, index) => ({
          message: safeString(issue && issue.message, `issues[${index}].message`, 500),
          sequence: safeOptionalUint32(issue && issue.sequence, `issues[${index}].sequence`)
        })) : [],
        events: cleanEvents
      };
    }

    validateHelloImport(hello) {
      return {
        boardVersion: safeByte(hello.boardVersion, 'hello.boardVersion'),
        rows: safeRange(hello.rows, 'hello.rows', 1, 32),
        cols: safeRange(hello.cols, 'hello.cols', 1, 32),
        overlapEnabled: !!hello.overlapEnabled,
        debounceUs: safeUint32(hello.debounceUs, 'hello.debounceUs'),
        overlapUs: safeUint32(hello.overlapUs, 'hello.overlapUs'),
        maxHoldUs: safeUint32(hello.maxHoldUs, 'hello.maxHoldUs'),
        keyPressMs: safeUint32(hello.keyPressMs, 'hello.keyPressMs'),
        streamId: safeUint32(hello.streamId, 'hello.streamId')
      };
    }

    validateEvent(e, index) {
      if (!e || typeof e !== 'object') throw new Error(`events[${index}] is not an object.`);
      const timestampMicros = e.timestampMicros === null || e.timestampMicros === undefined ? null : BigInt(String(e.timestampMicros));
      const queueKnown = !!e.queueKnown;
      return {
        id: safeOptionalPositiveInt(e.id, `events[${index}].id`) || index + 1,
        kind: KIND_SET.has(e.kind) ? e.kind : fail(`events[${index}].kind is invalid.`),
        title: safeString(e.title, `events[${index}].title`, 160),
        detail: safeString(e.detail, `events[${index}].detail`, 2000),
        sequence: safeOptionalUint32(e.sequence, `events[${index}].sequence`),
        typeName: safeString(e.typeName || 'LOCAL', `events[${index}].typeName`, 40),
        timestampMicros,
        deltaUs: e.deltaUs === null || e.deltaUs === undefined ? null : safeNonNegativeNumber(e.deltaUs, `events[${index}].deltaUs`),
        severity: safeString(e.severity || 'info', `events[${index}].severity`, 20),
        queueKnown,
        queueAfter: queueKnown ? this.validateQueueArray(e.queueAfter || [], `events[${index}].queueAfter`) : null,
        raw: e.raw === undefined ? null : e.raw
      };
    }

    validateQueueArray(queue, label) {
      if (!Array.isArray(queue)) throw new Error(`${label} must be an array.`);
      if (queue.length > MAX_QUEUE_ENTRIES) throw new Error(`${label} has ${queue.length} entries; max ${MAX_QUEUE_ENTRIES}.`);
      const ids = new Set();
      const slots = new Set();
      return queue.map((q, index) => {
        const item = {
          id: safeUint32(q && q.id, `${label}[${index}].id`),
          row: safeByte(q && q.row, `${label}[${index}].row`),
          col: safeByte(q && q.col, `${label}[${index}].col`),
          pressed: typeof (q && q.pressed) === 'boolean' ? q.pressed : fail(`${label}[${index}].pressed must be boolean.`),
          slot: safeByte(q && q.slot, `${label}[${index}].slot`),
          sampleMicros32: safeUint32(q && q.sampleMicros32, `${label}[${index}].sampleMicros32`)
        };
        if (ids.has(item.id) || slots.has(item.slot)) throw new Error(`${label} has duplicate id or slot.`);
        ids.add(item.id);
        slots.add(item.slot);
        return item;
      });
    }
  }

  const KIND_SET = new Set([INPUT, INTERNAL, OUTPUT]);

  function fail(message) {
    throw new Error(message);
  }

  function safeString(value, label, maxLength) {
    if (typeof value !== 'string') throw new Error(`${label} must be a string.`);
    if (value.length > maxLength) throw new Error(`${label} exceeds ${maxLength} characters.`);
    return value;
  }

  function safeRange(value, label, min, max) {
    if (!Number.isInteger(value) || value < min || value > max) throw new Error(`${label} must be ${min}..${max}.`);
    return value;
  }

  function safeByte(value, label) {
    return safeRange(value, label, 0, 255);
  }

  function safeUint32(value, label) {
    if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) throw new Error(`${label} must be uint32.`);
    return value;
  }

  function safeOptionalUint32(value, label) {
    if (value === null || value === undefined) return null;
    return safeUint32(value, label);
  }

  function safeOptionalPositiveInt(value, label) {
    if (value === null || value === undefined) return null;
    if (!Number.isInteger(value) || value < 1 || value > Number.MAX_SAFE_INTEGER) throw new Error(`${label} must be a positive integer.`);
    return value;
  }

  function safeNonNegativeNumber(value, label) {
    if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) throw new Error(`${label} must be a non-negative number.`);
    return value;
  }

  return {
    FRAME_SIZE,
    PAYLOAD_SIZE,
    MAGIC,
    TYPES,
    INPUT,
    INTERNAL,
    OUTPUT,
    REMOVE_REASONS,
    ACTIONS,
    MAX_IMPORT_BYTES,
    TraceParser,
    TraceModel,
    crc16CcittFalse
  };
});
