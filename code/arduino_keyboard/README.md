# Keyboard firmware and serial tracing

Target: Raspberry Pi Pico, **Arduino Mbed OS RP2040 Boards** (`arduino:mbed_rp2040:pico`).
The firmware uses the Mbed core's pluggable USB HID and CDC modules on the same
USB connection. It is not intended for the unrelated Earle Philhower RP2040 core.

Build with Arduino IDE or Arduino CLI (Mbed RP2040 core 4.6.0):

```powershell
arduino-cli core install arduino:mbed_rp2040@4.6.0
arduino-cli compile --fqbn arduino:mbed_rp2040:pico code\arduino_keyboard
```

Upload using the Pico's usual Arduino/BOOTSEL workflow. Opening the serial
debugger is optional: HID typing continues without it. Open
`code\remote\index.html` in desktop Chrome or Edge, select the keyboard's serial
port and close any other serial monitor first.

## Trace behavior

`REMOTE_TRACE` in `config.h` is enabled by default. It emits the KBT1 binary
protocol documented in `code\remote\README.md`. The interface is output-only:
there are no parameter-setting commands yet. Do not open it at 1200 baud, which
the Mbed core reserves for entering the bootloader.

- Events cover sampled switch changes, debounce/tap/overlap waits and decisions,
  queue removal, layer masks, I2C recovery attempts and exact HID reports.
- Input timestamps describe software observations, not exact electrical edges.
  HID success is the return value of the USB submission API, not proof that the
  OS/application received a key. A single `HID_OUTPUT` trace frame combines the
  keyboard and media reports, keeping both submission results and durations.
  The actual two USB HID reports are unchanged.
- A 24 KiB, fixed-size ring decouples instrumentation from USB. The loop makes
  at most four nonblocking `send_nb()` attempts per service call, preserving
  partially sent frames. It never waits for a debugger or a slow reader.
- Collection starts when CDC DTR is asserted. Closing the port discards pending
  telemetry. This is live capture, not a persistent pre-connection recorder.
- Reconnect starts a new stream with metadata and a queue checkpoint. Complete
  checkpoints are also attempted every two seconds; insufficient buffer space
  defers the whole checkpoint until space is available.
- Trace overflow drops new trace frames, increments the sequence and exposes a
  loss counter. The browser marks reconstruction unknown until a checkpoint.
  This is separate from the existing processing queue's overflow: that behavior
  is unchanged, but an explicit `QUEUE_OVERFLOW` marker now exposes it.
- The UI reconstructs the logical processing queue, not a dump of unused slots
  or deletion markers. Entries retain physical slot numbers and stable IDs.
- Timestamps extend the 32-bit microsecond clock in the main loop. As with other
  polling clock extensions, an entire clock wrap spent blocked (about 71 minutes)
  cannot be recovered. Existing HID sends, I2C operations and tap delays retain
  their existing behavior; the new CDC transmitter does not add blocking waits.

Legacy `DEBUG_LOG`, `I2C_RESET_LOG` and `PERF_LOG` text modes cannot be mixed with
the binary stream. Disable `REMOTE_TRACE` before enabling them.

## Host-side trace checks

With Clang and a working C++ standard library installed:

```powershell
clang++ -std=c++17 -Icode\tests\stubs code\tests\trace_test.cpp -o trace-test.exe
.\trace-test.exe
```

The test compiles the actual trace implementation against a USB stub, including
short/zero writes, CRC framing, timestamp wrap, queue-slot wrap, trace loss and
reconnect. An optional filename argument writes a binary capture for exercising
the browser decoder. This does not replace physical HID + CDC enumeration,
unplug/replug and typing checks on the Pico.
