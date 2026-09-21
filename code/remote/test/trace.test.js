const test = require('node:test');
const assert = require('node:assert/strict');
const trace = require('../src/trace.js');

function frame(type, seq, micros, fill = () => {}) {
  const bytes = new Uint8Array(trace.FRAME_SIZE);
  bytes.set(trace.MAGIC, 0);
  bytes[4] = type;
  const view = new DataView(bytes.buffer);
  view.setUint32(8, seq, true);
  view.setBigUint64(12, BigInt(micros), true);
  fill(new DataView(bytes.buffer, 20, trace.PAYLOAD_SIZE), bytes);
  view.setUint16(46, trace.crc16CcittFalse(bytes, 0, 46), true);
  return bytes;
}

function hello(seq = 1) {
  return frame(1, seq, 1000, p => {
    p.setUint8(0, 7); p.setUint8(1, 3); p.setUint8(2, 4); p.setUint8(3, 1);
    p.setUint32(4, 5000, true); p.setUint32(8, 30000, true); p.setUint32(12, 200000, true);
    p.setUint32(16, 120, true); p.setUint32(20, 99, true);
  });
}

function input(seq, id, row = 1, col = 2, slot = 0) {
  return frame(2, seq, 4000 + seq * 100, p => {
    p.setUint32(0, id, true); p.setUint32(4, 1234, true); p.setUint8(8, row); p.setUint8(9, col); p.setUint8(10, 1); p.setUint8(11, slot);
  });
}

function remove(seq, id, slot = 0) {
  return frame(3, seq, 4000 + seq * 100, p => {
    p.setUint32(0, id, true); p.setUint8(4, 1); p.setUint8(5, slot);
  });
}

function snapBegin(seq, count, checkpoint = 1) {
  return frame(6, seq, 2000 + seq, p => {
    p.setUint16(0, count, true); p.setUint8(2, 0); p.setUint8(3, count); p.setUint32(4, checkpoint, true);
  });
}

function snapBeginWithHead(seq, count, head, tail, checkpoint = 1) {
  return frame(6, seq, 2000 + seq, p => {
    p.setUint16(0, count, true); p.setUint8(2, head); p.setUint8(3, tail); p.setUint32(4, checkpoint, true);
  });
}

function snapEntry(seq, id, slot) {
  return frame(7, seq, 2000 + seq, p => {
    p.setUint32(0, id, true); p.setUint32(4, 1200, true); p.setUint8(8, 1); p.setUint8(9, 1); p.setUint8(10, 1); p.setUint8(11, slot);
  });
}

function hid(seq, report, success = 1) {
  return frame(5, seq, 3000 + seq, (p, b) => {
    p.setUint8(0, report.length); p.setUint8(1, success); p.setUint32(2, 25, true); b.set(report, 26);
  });
}

function snapEnd(seq, count, checkpoint = 1) {
  return frame(8, seq, 2000 + seq, p => {
    p.setUint16(0, count, true); p.setUint32(4, checkpoint, true);
  });
}

test('parser handles split frames and decodes HELLO', () => {
  const parser = new trace.TraceParser();
  const bytes = hello();
  assert.deepEqual(parser.push(bytes.slice(0, 17)), []);
  const records = parser.push(bytes.slice(17));
  assert.equal(records.length, 1);
  assert.equal(records[0].ok, true);
  assert.equal(records[0].frame.typeName, 'HELLO');
  assert.equal(records[0].frame.rows, 3);
  assert.equal(records[0].frame.streamId, 99);
});

test('parser reports corrupt frame then resynchronizes to next magic', () => {
  const parser = new trace.TraceParser();
  const bad = hello();
  bad[25] ^= 0xff;
  const records = parser.push(new Uint8Array([...bad, ...input(2, 10)]));
  assert.equal(records.some(r => !r.ok && r.error.includes('CRC mismatch')), true);
  assert.equal(records.at(-1).ok, true);
  assert.equal(records.at(-1).frame.typeName, 'INPUT');
});

test('complete snapshot atomically establishes queue and hides entries', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBegin(2, 2), snapEntry(3, 11, 0), snapEntry(4, 12, 1), snapEnd(5, 2)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, true);
  assert.deepEqual(model.queue.map(q => q.id), [11, 12]);
  assert.equal(model.events.some(e => e.typeName === 'SNAPSHOT_ENTRY'), false);
  assert.equal(model.events.at(-1).title, 'Queue checkpoint');
  assert.equal(model.events.at(-1).queueAfter.length, 2);
});

test('checkpoint preserves wrapped ring order', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBeginWithHead(2, 3, 254, 1), snapEntry(3, 11, 254), snapEntry(4, 12, 255), snapEntry(5, 13, 0), snapEnd(6, 3)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, true);
  assert.deepEqual(model.queue.map(q => q.slot), [254, 255, 0]);
});

test('checkpoint accepts wrapped live entries separated by deletion tombstones', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBeginWithHead(2, 2, 254, 1), snapEntry(3, 11, 254), snapEntry(4, 13, 0), snapEnd(5, 2)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, true);
  assert.deepEqual(model.queue.map(q => q.slot), [254, 0]);
});

test('snapshot malformed bounds and duplicate slots invalidate queue', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBeginWithHead(2, 2, 0, 2), snapEntry(3, 11, 0), snapEntry(4, 12, 0), snapEnd(5, 2)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, false);
  assert.equal(model.events.some(e => e.detail.includes('duplicate id/slot')), true);
});

test('sequence gap invalidates queue until checkpoint', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBegin(2, 0), snapEnd(3, 0), input(5, 77)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, false);
  assert.equal(model.events.some(e => e.title === 'Sequence gap'), true);
});

test('interleaved frame rejects pending checkpoint', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBegin(2, 1), input(3, 90), snapEntry(4, 91, 0), snapEnd(5, 1)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, false);
  assert.equal(model.events.some(e => e.title === 'Checkpoint interrupted'), true);
  assert.equal(model.events.at(-1).title, 'Checkpoint rejected');
});

test('input and remove mutate known queue', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBegin(2, 0), snapEnd(3, 0), input(4, 42), remove(5, 42)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, true);
  assert.equal(model.queue.length, 0);
  assert.equal(model.events.find(e => e.title.includes('Key press')).queueAfter.length, 1);
});

test('input duplicate and remove mismatch invalidate known queue', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBeginWithHead(2, 1, 0, 1), snapEntry(3, 31, 0), snapEnd(4, 1), input(5, 32, 1, 2, 0)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, false);

  const model2 = new trace.TraceModel();
  const parser2 = new trace.TraceParser();
  for (const bytes of [hello(1), snapBeginWithHead(2, 1, 0, 1), snapEntry(3, 31, 0), snapEnd(4, 1), remove(5, 31, 1)]) {
    for (const record of parser2.push(bytes)) model2.applyRecord(record);
  }
  assert.equal(model2.queueKnown, false);
  assert.match(model2.events.at(-1).detail, /did not match/);
});

test('coordinates are validated against HELLO dimensions', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBegin(2, 0), snapEnd(3, 0), input(4, 1, 9, 0)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, false);
  assert.equal(model.events.at(-1).severity, 'error');
});

test('retention is capped', () => {
  const model = new trace.TraceModel({ maxEvents: 3 });
  for (let i = 0; i < 6; i++) model.addIssue(`issue ${i}`, null);
  assert.equal(model.events.length, 3);
  assert.equal(model.events[0].detail, 'issue 3');
});

test('export and import restore queue and events', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBegin(2, 1), snapEntry(3, 31, 0), snapEnd(4, 1)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  const imported = new trace.TraceModel();
  imported.importState(JSON.parse(JSON.stringify(model.serialize())));
  assert.equal(imported.queueKnown, true);
  assert.equal(imported.queue[0].id, 31);
  assert.equal(imported.events.at(-1).timestampMicros, 2004n);
});

test('parser reports unsupported KBT version explicitly', () => {
  const parser = new trace.TraceParser();
  const records = parser.push(new Uint8Array([0x4b, 0x42, 0x54, 0x32, 1, 2, 3, 4]));
  assert.equal(records.some(r => !r.ok && r.error.includes('supports KBT1 only')), true);
});

test('malformed payloads and parser errors invalidate queue immediately', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBegin(2, 0), snapEnd(3, 0)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, true);
  const badPressed = input(4, 1);
  badPressed[30] = 2;
  new DataView(badPressed.buffer).setUint16(46, trace.crc16CcittFalse(badPressed, 0, 46), true);
  for (const record of parser.push(badPressed)) model.applyRecord(record);
  assert.equal(model.queueKnown, false);
  assert.equal(model.pendingSnapshot, null);
});

test('HID output interprets keyboard press and release differences', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [
    hello(1),
    hid(2, [1, 0, 0, 4, 0, 0, 0, 0, 0]),
    hid(3, [1, 0, 0, 0, 0, 0, 0, 0, 0])
  ]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.match(model.events.at(-2).detail, /keyboard baseline/);
  assert.match(model.events.at(-1).detail, /released 0x04/);
});

test('import is atomic and validates unsafe data', () => {
  const model = new trace.TraceModel();
  model.addIssue('before', null);
  assert.throws(() => model.importState({ format: 'KBT_TRACE_MODEL_V1', queue: [{ id: 1, slot: 0 }], events: [{ id: 1, kind: 'NOPE' }] }));
  assert.equal(model.events.length, 1);
  assert.equal(model.events[0].detail, 'before');
});
