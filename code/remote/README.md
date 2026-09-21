# Keyboard Trace Timeline

Standalone local Web Serial viewer for the custom keyboard KBT1 trace stream. Open `index.html` by double-clicking it, or host the same file over HTTPS. There are no runtime network dependencies, module imports, service workers, framework bundles or telemetry. Only the display-layout preference is saved in browser storage; captures are not stored automatically.

## Build and test

```powershell
cd code\remote
node build.mjs
node --test
```

`src\trace.js`, `src\app.js`, and `src\styles.css` are the classic sources. `index.html` is the tracked generated artifact.

## Connect

1. Use Edge or Chrome with Web Serial support.
2. Open `index.html` locally or from HTTPS.
3. Click **Connect keyboard** and select the serial device.
4. The app opens at `115200` baud and sets DTR `true`.

The app never uses 1200-baud reset and never writes commands to the device. If no valid `HELLO` arrives shortly after opening, the UI warns about wrong/old firmware or port selection. Use **Demo trace** without hardware. Import/demo are disabled while live capture is active to avoid mixing sources.

Trace data can reveal typed keys. Exports are JSON and stay local unless you share them.

The app fits the viewport; the timeline and details panel scroll independently.
Input/output rows have one-line summaries; click a row for timestamps, report
bytes, submission results, transitions and queue state. Visible row/column
numbers start at 1; raw protocol coordinates start at 0. Output summaries show
the currently reported pressed keys using the selected **Layout**:
**English (US QWERTY)** or **French (France AZERTY)** (traditional PC layout).
French labels include A/Q, Z/W, M and punctuation/accent keys, and AltGr.
This changes the viewer only, not the firmware or the computer's keyboard layout.
Output shows the translated symbol first, followed by the physical combination
in gray: `OUTPUT { (AltGr, ')`. Shift and French AltGr (including Windows
Ctrl+Alt) are interpreted. The parentheses retain unmodified key labels.
Letters assume Caps Lock off; keypad keys retain their names because Num Lock
is not captured. Shortcuts are labeled `Shortcut`, dead keys `Dead`, and
unsupported modifier levels `Unmapped`. This is a preview of each held key,
not an ordered text stream or confirmation of host input. Dead-key/IME
composition is not inferred. Failed submissions remain visibly marked.

Changing Layout immediately relabels retained/imported events and their inspector
headings. Exported summaries use the selected layout while raw HID bytes remain
unchanged; importing uses your current display preference. English is the initial
default. The selection is remembered for the current site/local-file context when
browser storage is available; otherwise a session-only notice is shown.

### Layout library choice

No new runtime dependency is needed for these two layouts.
[Keyboard Map API](https://wicg.github.io/keyboard-map/#h-keyboard-getlayoutmap)
only exposes unmodified labels, not the Shift/AltGr levels required here.
[simple-keyboard-layouts](https://github.com/hodgef/simple-keyboard-layouts)
provides onscreen keyboard rows rather than a HID/modifier decoder;
[native-keymap](https://github.com/microsoft/node-native-keymap) requires native
Node bindings and cannot run in a standalone browser file.
[hid-io/layouts](https://github.com/hid-io/layouts) is a useful data source if
more layouts are added, but is not a browser text-input engine.

The explicit French levels follow the traditional Windows
[KBDFR shift states](https://kbdlayout.info/KBDfr/shiftstates).
Keeping the small tables in the bundle works offline and permits deterministic
replay using the selected layout, independent of the viewer's current OS layout.

Update the firmware to get one combined keyboard/media trace event per output.
The viewer also accepts the older individual type-5 reports and saved captures.

## KBT1 frame format

Every frame is exactly 48 bytes:

| Offset | Field |
| --- | --- |
| 0..3 | ASCII magic `KBT1` |
| 4 | type |
| 5..7 | reserved, must be zero |
| 8..11 | sequence `uint32LE`, increments for every attempted frame including drops |
| 12..19 | extended device timestamp, microseconds, `uint64LE` |
| 20..45 | 26-byte payload, zero padded |
| 46..47 | CRC16-CCITT-FALSE `uint16LE`, polynomial `0x1021`, init `0xffff`, over bytes 0..45 |

Unsupported `KBTx` versions, CRC errors, garbage, invalid reserved bytes, unknown types, sequence gaps, and malformed payload values are surfaced as timeline errors. Parser errors immediately invalidate queue state.

## Payloads

Offsets below are relative to payload byte 20.

### `1 HELLO`

`boardVersion u8@0`, `rows u8@1`, `cols u8@2`, `overlapEnabled u8@3` (`0/1`), `debounceUs u32@4`, `overlapUs u32@8`, `maxHoldUs u32@12`, `keyPressMs u32@16`, `streamId u32@20`.

Sent on start and at periodic 2s checkpoints. Repeated `HELLO` does not erase the prior timeline and does not establish queue contents; only a complete snapshot does.

### `2 INPUT` and `7 SNAPSHOT_ENTRY`

`id u32@0`, `sampleMicros32 u32@4`, `row u8@8`, `col u8@9`, `pressed u8@10` (`0/1`), `queue slot u8@11`.

Rows/cols are checked after `HELLO`. Queue order is ring order from checkpoints, including wrap (`254,255,0`), not numeric sort.

### `3 REMOVE`

`id u32@0`, `reason u8@4`, `slot u8@5`.

Reasons: `1 processed`, `2 debounce`, `3 tap`, `4 overlap`. Removal must match both id and slot when queue is known.

### `4 DECISION`

`primaryID u32@0`, `relatedID u32@4`, `action u8@8`, `layerMask u8@9`, `interveningCount u8@10`.

Actions: `1 debounceWait`, `2 debounceCancel`, `3 overlapWait`, `4 overlapTap`, `5 tapWait`, `6 tap`, `7 hold(no tap)`, `8 process`, `9 layer`.

### `5 HID`

Legacy/standalone single-report record. Normal keyboard output now uses type 12.

`reportLength u8@0`, `success u8@1` (`0/1`), `sendDurationUs u32@2`, raw report bytes at payload `@6`.

Valid reports are keyboard ID `1` length `9` (`ID, modifier, reserved, six key bytes`) and media ID `3` length `2` (`ID, bitmask`). The UI interprets modifier/key/media state and highlights press/release differences after the first successful report. Success means API submission only, not host receipt.

### `6 SNAPSHOT_BEGIN` / `8 SNAPSHOT_END`

Begin: `count u16@0`, `head u8@2`, `tail u8@3`, `checkpointID u32@4`.

End: `count u16@0`, `checkpointID u32@4`.

Counts must be `<=255`. A snapshot publishes atomically only when begin, exactly `count` entries, and matching end are uninterrupted, ids/slots are unique, and entries follow head/tail wrap order.

### `9 LOSS`, `10 I2C_RESET`, `11 QUEUE_OVERFLOW`

- `LOSS`: cumulative dropped trace frames `u32@0`; queue becomes unknown.
- `I2C_RESET`: `chipIndex u8@0`.
- `QUEUE_OVERFLOW`: `head u8@0`, `tail u8@1`; observer only, queue becomes unknown until checkpoint.

### `12 HID_OUTPUT`

One trace event for one keyboard output update, after both USB reports have
been submitted. USB still receives separate keyboard and media reports.

`keyboardSuccess u8@0`, `mediaSuccess u8@1` (each `0/1`),
`keyboardDurationUs u32@2`, `mediaDurationUs u32@6`,
keyboard report (9 bytes, ID `1`) at `@10`, media report (2 bytes, ID `3`) at `@19`.

Both success flags and durations remain available in the details inspector;
a partial failure is never presented as a successful combined update.
Media bits match the firmware descriptor: next track, previous track, stop,
play/pause, mute, volume up, volume down (bits 0..6).
