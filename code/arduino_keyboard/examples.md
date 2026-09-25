# Key press examples

Real key press sequences, taken from traces, with the output we expect. Use them to check any change
to the event processing (debouncing, "on release" keys, overlap removal, layers).

Positions are given as `row, column`, one-based, as shown in the trace tool. Times are relative to
the first event of the example. The layout is AZERTY.

## Typing "I'm"

Keys used:

| Position | Base layer | Layer when held | Relevant output |
|---|---|---|---|
| 5, 5 | Shift | Shift layer | |
| 2, 9 | `i` | | `I` on the Shift layer |
| 5, 6 | Space (on tap) | Accent layer | |
| 2, 6 | `t` | | `'` on the Accent layer (`D4`) |
| 3, 11 | `m` | | |

Events:

| Time | Event |
|---|---|
| 0 ms | press Shift (5, 5) |
| 272.6 ms | press `i` (2, 9) |
| 347.6 ms | release Shift (5, 5) |
| 372.1 ms | release `i` (2, 9) |
| 476.9 ms | press Space (5, 6) |
| 525.8 ms | press `t` (2, 6) |
| 541.2 ms | release Space (5, 6) |
| 625.4 ms | release `t` (2, 6) |
| 625.5 ms | press `m` (3, 11) |
| 735.0 ms | release `m` (3, 11) |

Expected output: `I'm`

- Shift is released before `i`, but after `i` was pressed, so `i` comes out as `I`.
- Space is held while `t` is pressed, so this is a use of the Accent layer and `t` comes out as `'`.
  No space is typed. Space is released only 15.5 ms after `t` was pressed, which is a quick
  overlap, but it is a deliberate one.
- `m` is pressed after Space was released, so it comes out on the base layer.

Wrong output seen with overlap removal enabled (30 ms threshold): `I tm`. The quick release of
Space was taken as a late release of a Space tap.
