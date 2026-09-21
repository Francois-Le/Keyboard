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

function hello(seq = 1, layerCount = 0, streamId = 99, time = 1000) {
  return frame(1, seq, time, p => {
    p.setUint8(0, 7); p.setUint8(1, 3); p.setUint8(2, 4); p.setUint8(3, 1);
    p.setUint32(4, 5000, true); p.setUint32(8, 30000, true); p.setUint32(12, 200000, true);
    p.setUint32(16, 120, true); p.setUint32(20, streamId, true); p.setUint16(24, layerCount, true);
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

function output(seq, keyboard = [1, 4, 0, 4, 24, 0, 0, 0, 0], media = [3, 0], keyboardSuccess = 1, mediaSuccess = 1) {
  return frame(12, seq, 3000 + seq, (p, b) => {
    p.setUint8(0, keyboardSuccess);
    p.setUint8(1, mediaSuccess);
    p.setUint32(2, 25, true);
    p.setUint32(6, 10, true);
    b.set(keyboard, 30);
    b.set(media, 39);
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
  assert.equal(model.events[0].kind, 'CHECKPOINT');
  assert.equal(model.events.at(-1).kind, 'CHECKPOINT');
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
  assert.equal(model.events.find(e => e.title === 'Sequence gap').kind, 'INTERNAL');
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
  assert.equal(model.events.at(-1).kind, 'INTERNAL');
});

test('input and remove mutate known queue', () => {
  const parser = new trace.TraceParser();
  const model = new trace.TraceModel();
  for (const bytes of [hello(1), snapBegin(2, 0), snapEnd(3, 0), input(4, 42), remove(5, 42)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.queueKnown, true);
  assert.equal(model.queue.length, 0);
  assert.equal(model.events.find(e => e.typeName === 'INPUT').queueAfter.length, 1);
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
  assert.equal(imported.events[0].kind, 'CHECKPOINT');
  assert.equal(imported.events.at(-1).kind, 'CHECKPOINT');
});

test('older exports migrate successful checkpoints without hiding errors', () => {
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  for (const bytes of [hello(1), snapBegin(2, 0), snapEnd(3, 0), snapEnd(4, 0)]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  const state = JSON.parse(JSON.stringify(model.serialize()));
  state.events.forEach(event => { event.kind = 'INTERNAL'; });
  const imported = new trace.TraceModel();
  imported.importState(state);
  assert.deepEqual(imported.events.map(event => event.kind), ['CHECKPOINT', 'CHECKPOINT', 'INTERNAL']);
  assert.equal(imported.events.at(-1).title, 'Checkpoint rejected');
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

test('input summary is one line with one-based coordinates and preserves raw details', () => {
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  for (const record of parser.push(input(1, 42, 4, 1))) model.applyRecord(record);
  assert.equal(model.events[0].title, 'press row 5 column 2');
  assert.match(model.events[0].detail, /raw row 4, col 1/);
});

test('combined keyboard and media produces exactly one readable output event', () => {
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  for (const record of parser.push(output(1))) model.applyRecord(record);
  assert.equal(model.events.length, 1);
  const event = model.events[0];
  assert.equal(event.kind, 'OUTPUT');
  assert.equal(event.title, 'Shortcut (Alt, A, U)');
  assert.match(event.detail, /Keyboard submitted \(25µs\)/);
  assert.match(event.detail, /Media submitted \(10µs\)/);
  assert.deepEqual(event.raw.keyboardReport, [1, 4, 0, 4, 24, 0, 0, 0, 0]);
  assert.deepEqual(event.raw.mediaReport, [3, 0]);
  for (const record of parser.push(output(2, [1, 0, 0, 0, 0, 0, 0, 0, 0]))) model.applyRecord(record);
  assert.equal(model.events.length, 2);
  assert.equal(model.events[1].title, 'Released');
  assert.match(model.events[1].detail, /released Alt, 0x04, 0x18/);
});

test('combined output uses descriptor media bit order and exposes partial send failures', () => {
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  for (const record of parser.push(output(1, [1, 0, 0, 0, 0, 0, 0, 0, 0], [3, 0x29], 1, 0))) model.applyRecord(record);
  assert.equal(model.events[0].title, 'Failed: Next track, Play/Pause, Volume up (Next track, Play/Pause, Volume up)');
  assert.equal(model.events[0].severity, 'warn');
  assert.match(model.events[0].detail, /Media FAILED/);
  assert.equal(model.lastHidReports.has('1'), true);
  assert.equal(model.lastHidReports.has('3'), false);
});

test('combined output rejects malformed success flags and report IDs', () => {
  for (const bytes of [output(1, undefined, undefined, 2), output(1, undefined, [1, 0])]) {
    const model = new trace.TraceModel();
    for (const record of new trace.TraceParser().push(bytes)) model.applyRecord(record);
    assert.equal(model.events[0].severity, 'error');
    assert.equal(model.events[0].kind, 'INTERNAL');
    assert.equal(model.lastHidReports.size, 0);
  }
});

test('verbose named output still round-trips capture export', () => {
  const model = new trace.TraceModel();
  for (const record of new trace.TraceParser().push(output(1, [1, 255, 0, 250, 249, 248, 247, 246, 245], [3, 127]))) model.applyRecord(record);
  const imported = new trace.TraceModel();
  imported.importState(JSON.parse(JSON.stringify(model.serialize())));
  assert.equal(imported.events[0].title, model.events[0].title);
});

test('French layout labels letters, punctuation and number-row keys', () => {
  const cases = [
    [4, 'Q'], [20, 'A'], [26, 'Z'], [29, 'W'], [51, 'M'], [16, ','],
    [30, '&'], [31, 'é'], [32, '"'], [33, "'"], [34, '('], [35, '-'],
    [36, 'è'], [37, '_'], [38, 'ç'], [39, 'à'],
    [45, ')'], [46, '='], [47, '^'], [48, '$'], [49, '*'], [50, '*'],
    [52, 'ù'], [53, '²'], [54, ';'], [55, ':'], [56, '!'], [100, '<'],
    [40, 'Enter'], [58, 'F1'], [89, 'Keypad 1'], [230, 'AltGr'], [250, 'HID 0xfa']
  ];
  for (const [usage, name] of cases) assert.equal(trace.keyName(usage, 'fr-FR'), name);
  assert.equal(trace.keyName(4, 'en-US'), 'A');
  assert.equal(trace.keyName(20, 'en-US'), 'Q');
  assert.equal(trace.keyName(31, 'en-US'), '2');
});

test('layout changes relabel existing and imported output without mutating raw data', () => {
  const model = new trace.TraceModel();
  for (const record of new trace.TraceParser().push(output(1))) model.applyRecord(record);
  const saved = JSON.stringify(model.serialize());
  assert.equal(trace.eventTitle(model.events[0], 'fr-FR'), 'Shortcut (Alt, Q, U)');
  assert.equal(trace.eventTitle(model.events[0], 'en-US'), 'Shortcut (Alt, A, U)');
  assert.deepEqual(model.serialize().events, JSON.parse(saved).events);
  const imported = new trace.TraceModel();
  imported.importState(JSON.parse(saved));
  assert.equal(trace.eventTitle(imported.events[0], 'fr-FR'), 'Shortcut (Alt, Q, U)');
  assert.throws(() => trace.eventTitle(imported.events[0], 'unknown'), /Unsupported keyboard layout/);
});

test('French layout covers legacy reports, AltGr, media and failed outputs', () => {
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  for (const record of parser.push(hid(1, [1, 64, 0, 20, 51, 31, 0, 0, 0]))) model.applyRecord(record);
  assert.equal(trace.eventTitle(model.events[0], 'fr-FR'), 'Unmapped, Unmapped, Dead ~ (AltGr, A, M, é)');
  for (const record of parser.push(output(2, [1, 0, 0, 4, 0, 0, 0, 0, 0], [3, 32], 0, 1))) model.applyRecord(record);
  assert.equal(trace.eventTitle(model.events[1], 'fr-FR'), 'Failed: q, Volume up (Q, Volume up)');
  assert.equal(trace.eventTitle({ kind: 'INPUT', title: 'press row 1 column 1' }, 'fr-FR'), 'press row 1 column 1');
  assert.equal(trace.eventTitle({ kind: 'OUTPUT', title: 'old capture', raw: { type: 12 } }, 'fr-FR'), 'old capture');
});

test('import is atomic and validates unsafe data', () => {
  const model = new trace.TraceModel();
  model.addIssue('before', null);
  assert.throws(() => model.importState({ format: 'KBT_TRACE_MODEL_V1', queue: [{ id: 1, slot: 0 }], events: [{ id: 1, kind: 'NOPE' }] }));
  assert.equal(model.events.length, 1);
  assert.equal(model.events[0].detail, 'before');
});

function translated(layout, modifiers, code, success = 1, legacy = false) {
  const model = new trace.TraceModel();
  const report = [1, modifiers, 0, code, 0, 0, 0, 0, 0];
  const bytes = legacy ? hid(1, report, success) : output(1, report, [3, 0], success);
  for (const record of new trace.TraceParser().push(bytes)) model.applyRecord(record);
  return trace.eventPresentation(model.events[0], layout);
}

test('French AltGr apostrophe gives the requested short presentation', () => {
  assert.deepEqual(translated('fr-FR', 64, 33), { primary: '{', combination: "(AltGr, ')" });
  assert.deepEqual(translated('fr-FR', 64, 33, 1, true), { primary: '{', combination: "(AltGr, ')" });
  assert.equal(translated('fr-FR', 65, 33).primary, '{');
  assert.equal(translated('fr-FR', 5, 33).primary, '{');
  assert.equal(translated('fr-FR', 64, 33, 0).primary, 'Failed: {');
});

test('French AltGr symbols and dead keys follow the selected modifier level', () => {
  for (const [usage, expected] of [
    [8, '€'], [31, 'Dead ~'], [32, '#'], [33, '{'], [34, '['], [35, '|'],
    [36, 'Dead `'], [37, '\\'], [38, '^'], [39, '@'], [45, ']'], [46, '}'], [48, '¤']
  ]) assert.equal(translated('fr-FR', 64, usage).primary, expected);
  assert.equal(translated('fr-FR', 0, 47).primary, 'Dead ^');
  assert.equal(translated('fr-FR', 2, 47).primary, 'Dead ¨');
  assert.equal(translated('fr-FR', 64, 4).primary, 'Unmapped');
  assert.equal(translated('fr-FR', 66, 33).primary, 'Unmapped');
});

test('Shift maps US and French numbers, symbols and letter case', () => {
  for (const [usage, us, fr] of [
    [30, '!', '1'], [31, '@', '2'], [32, '#', '3'], [33, '$', '4'], [34, '%', '5'],
    [35, '^', '6'], [36, '&', '7'], [37, '*', '8'], [38, '(', '9'], [39, ')', '0'],
    [45, '_', '°'], [46, '+', '+'], [48, '}', '£'], [52, '"', '%'], [55, '>', '/']
  ]) {
    assert.equal(translated('en-US', 2, usage).primary, us);
    assert.equal(translated('fr-FR', 32, usage).primary, fr);
  }
  assert.deepEqual(translated('fr-FR', 0, 20), { primary: 'a', combination: '(A)' });
  assert.deepEqual(translated('fr-FR', 2, 20), { primary: 'A', combination: '(Shift, A)' });
  assert.equal(translated('fr-FR', 0, 51).primary, 'm');
  assert.equal(translated('fr-FR', 2, 16).primary, '?');
  assert.equal(translated('en-US', 0, 4).primary, 'a');
  assert.equal(translated('en-US', 2, 47).primary, '{');
});

test('shortcut, modifier, release and named-key rows do not claim typed text', () => {
  assert.equal(translated('en-US', 64, 33).primary, 'Shortcut');
  assert.equal(translated('fr-FR', 1, 6).primary, 'Shortcut');
  assert.equal(translated('fr-FR', 8, 6).primary, 'Shortcut');
  assert.equal(translated('fr-FR', 64, 0).primary, 'Modifier');
  assert.deepEqual(translated('fr-FR', 0, 0), { primary: 'Released', combination: '' });
  assert.equal(translated('fr-FR', 0, 40).primary, 'Enter');
  assert.equal(translated('fr-FR', 0, 89).primary, 'Keypad 1');
  assert.equal(translated('en-US', 0, 250).primary, 'HID 0xfa');
});

test('only debounce, overlap and layer decisions are INTERNAL; queue bookkeeping is DETAILS', () => {
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  const feed = bytes => { for (const record of parser.push(bytes)) model.applyRecord(record); };
  for (const bytes of [hello(1), snapBegin(2, 0), snapEnd(3, 0), input(4, 42), remove(5, 42)]) feed(bytes);
  assert.equal(model.events.at(-1).kind, 'DETAILS');
  assert.equal(model.queueKnown, true);
  assert.equal(model.queue.length, 0);
  for (let action = 1; action <= 9; action++) {
    feed(frame(4, action + 5, 5000 + action * 100, p => {
      p.setUint32(0, 42, true);
      p.setUint8(8, action);
      p.setUint8(9, 1);
    }));
    assert.equal(model.events.at(-1).kind, [2, 4, 9].includes(action) ? 'INTERNAL' : 'DETAILS');
  }
  assert.deepEqual(model.events.filter(e => e.kind === 'INTERNAL').map(e => e.title),
    ['Debounced key pair', 'Overlap removed', 'Layer changed']);
  feed(frame(4, 15, 7000, p => p.setUint8(8, 255)));
  assert.equal(model.events.at(-1).kind, 'INTERNAL');
  assert.equal(model.events.at(-1).severity, 'error');
  feed(frame(10, 16, 7100, p => p.setUint8(0, 2)));
  assert.equal(model.events.at(-1).kind, 'INTERNAL');

  const exported = JSON.parse(JSON.stringify(model.serialize()));
  const imported = new trace.TraceModel();
  imported.importState(exported);
  assert.deepEqual(imported.events.map(e => e.kind), model.events.map(e => e.kind));
  exported.events.forEach(e => { if (e.kind === 'DETAILS') e.kind = 'INTERNAL'; });
  imported.importState(exported);
  assert.deepEqual(imported.events.map(e => e.kind), model.events.map(e => e.kind));
});

test('diagnostic summaries keep basic context without replacing inspection details', () => {
  const cases = [
    ['INTERNAL', { type: 4, action: 2, primaryID: 42, relatedID: 43 }, 'Debounced key pair #42 / #43'],
    ['INTERNAL', { type: 4, action: 4, primaryID: 42, relatedID: 43 }, 'Overlap removed #42 / #43'],
    ['INTERNAL', { type: 4, action: 9, primaryID: 42, layerMask: 3 }, 'Layer changed: 0x03'],
    ['DETAILS', { type: 4, action: 1, primaryID: 42 }, 'debounce wait #42'],
    ['DETAILS', { type: 3, id: 42, reason: 2 }, 'Remove #42 (debounce)'],
    ['CHECKPOINT', { type: 1, streamId: 7 }, 'Stream metadata: stream 7'],
    ['CHECKPOINT', { type: 8, count: 2 }, 'Queue checkpoint: 2 pending']
  ];
  for (const [kind, raw, expected] of cases) {
    const event = { kind, raw, title: 'Original title', severity: 'info', detail: 'Full detail\nwith extra context' };
    assert.equal(trace.eventTitle(event), expected);
    assert.equal(trace.eventPresentation(event).combination, '');
    assert.equal(event.detail, 'Full detail\nwith extra context');
    assert.equal(event.title, 'Original title');
  }
  assert.equal(trace.eventTitle({ kind: 'INTERNAL', raw: { type: 3, id: 42, reason: 2 }, severity: 'error', title: 'Parser/protocol issue' }), 'Parser/protocol issue');
});

function layerMetadata(seq, mask, name, time = 1000) {
  return frame(13, seq, time, (p, bytes) => {
    const text = new TextEncoder().encode(name);
    p.setUint8(0, mask);
    p.setUint8(1, text.length);
    bytes.set(text.slice(0, 24), 22);
  });
}

function layerChange(seq, mask, time = 2000 + seq) {
  return frame(4, seq, time, p => {
    p.setUint32(0, 42, true);
    p.setUint8(8, 9);
    p.setUint8(9, mask);
  });
}

test('firmware metadata names effective masks and remains attached to exported events', () => {
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  const feed = bytes => { for (const record of parser.push(bytes)) model.applyRecord(record); };
  const names = ['Base', 'Shift', 'Function', 'Function', 'Accent', 'Accent', 'Accent 2', 'Accent 2'];
  feed(hello(1, names.length));
  let seq = 2;
  names.forEach((name, mask) => feed(layerMetadata(seq++, mask, name)));
  assert.equal(model.events.length, 1); // Name records do not add noisy timeline rows.
  names.forEach((name, mask) => {
    feed(layerChange(seq++, mask));
    const event = model.events.at(-1);
    assert.equal(event.layerName, name);
    assert.equal(trace.eventTitle(event), `Layer changed: ${name}`);
    assert.equal(event.raw.layerMask, mask);
    assert.ok(event.detail.includes(`layer mask 0x${mask.toString(16)}`));
  });
  const exported = JSON.parse(JSON.stringify(model.serialize()));
  const imported = new trace.TraceModel();
  imported.importState(exported);
  assert.equal(imported.layerNames[6], 'Accent 2');
  assert.equal(trace.eventTitle(imported.events.at(-1)), 'Layer changed: Accent 2');

  const oldEvent = model.events.at(-1);
  model.beginLiveSession();
  feed(layerChange(1, 6, 3000));
  assert.equal(trace.eventTitle(model.events.at(-1)), 'Layer changed: 0x06');
  feed(hello(2, 1, 123, 4000));
  feed(layerMetadata(3, 0, 'Custom base', 4000));
  feed(layerChange(4, 0, 4100));
  assert.equal(trace.eventTitle(model.events.at(-1)), 'Layer changed: Custom base');
  assert.equal(trace.eventTitle(oldEvent), 'Layer changed: Accent 2');
});

test('partial or lost layer metadata never supplies stale or guessed names', () => {
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  const feed = bytes => { for (const record of parser.push(bytes)) model.applyRecord(record); };
  feed(hello(1, 2));
  feed(layerMetadata(2, 0, 'Base'));
  assert.deepEqual(model.layerNames, {});
  feed(layerChange(3, 0));
  assert.equal(trace.eventTitle(model.events.at(-1)), 'Layer changed: 0x00');
  assert.ok(model.events.some(event => event.detail.includes('metadata interrupted')));

  feed(hello(4, 2, 99, 3000));
  feed(layerMetadata(5, 0, 'Base', 3000));
  feed(layerMetadata(7, 1, 'Shift', 3000)); // Missing sequence 6 invalidates the whole set.
  feed(layerChange(8, 0, 3100));
  assert.equal(trace.eventTitle(model.events.at(-1)), 'Layer changed: 0x00');
  feed(hello(9, 1, 99, 4000));
  feed(layerMetadata(10, 0, 'Recovered base', 4000));
  feed(layerChange(11, 0, 4100));
  assert.equal(trace.eventTitle(model.events.at(-1)), 'Layer changed: Recovered base');
});

test('bad layer metadata is rejected visibly, including duplicate masks', () => {
  for (const bad of [
    layerMetadata(2, 0, ''),
    layerMetadata(2, 0, 'x'.repeat(25)),
    layerMetadata(2, 0, 'bad\nname'),
    layerMetadata(2, 1, 'Wrong order')
  ]) {
    const model = new trace.TraceModel();
    const parser = new trace.TraceParser();
    for (const bytes of [hello(1, 2), bad]) for (const record of parser.push(bytes)) model.applyRecord(record);
    assert.equal(model.events.at(-1).severity, 'error');
    assert.deepEqual(model.layerNames, {});
  }
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  for (const bytes of [hello(1, 2), layerMetadata(2, 0, 'Base'), layerMetadata(3, 0, 'Duplicate')]) {
    for (const record of parser.push(bytes)) model.applyRecord(record);
  }
  assert.equal(model.events.at(-1).severity, 'error');
  assert.deepEqual(model.layerNames, {});
});

test('old captures fall back to masks and imported layer names are validated atomically', () => {
  const model = new trace.TraceModel();
  const parser = new trace.TraceParser();
  for (const bytes of [hello(), layerChange(2, 6)]) for (const record of parser.push(bytes)) model.applyRecord(record);
  assert.equal(trace.eventTitle(model.events.at(-1)), 'Layer changed: 0x06');
  const old = JSON.parse(JSON.stringify(model.serialize()));
  delete old.layerNames;
  delete old.hello.layerCount;
  old.events.forEach(event => { delete event.layerName; });
  const imported = new trace.TraceModel();
  imported.importState(old);
  assert.equal(trace.eventTitle(imported.events.at(-1)), 'Layer changed: 0x06');
  assert.throws(() => imported.importState({ ...old, layerNames: { 256: 'Bad mask' } }));
  assert.throws(() => imported.importState({ ...old, layerNames: { 6: 'x'.repeat(25) } }));
  assert.equal(trace.eventTitle(imported.events.at(-1)), 'Layer changed: 0x06');
});
