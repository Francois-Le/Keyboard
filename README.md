Complete custom keyboard with pcb design, 3D printable casing and firmware for a raspberry pi pico. Uses kailh low profile switches.

*Warning:* PCB_v2 may have some design mistake, I can't remember if I fixed them.

The firmware in `code/arduino_keyboard` provides HID keyboard output and an
optional binary USB serial trace. See its README for Arduino Mbed Pico build
instructions.

Open `code/remote/index.html` in desktop Chrome or Edge for the Web Serial
debugger: a vertical input/decision/HID timeline with queue inspection. It works
as a local file or a hosted HTTPS page; **Demo trace** works without hardware.
The remote README describes the protocol, capture export/import and development.
