#pragma once
#include <algorithm>
#include <stdint.h>
#include <vector>

struct TestSerial {
  bool isConnected = false;
  uint32_t writeLimit = 48;
  uint32_t calls = 0;
  unsigned long baud = 0;
  std::vector<uint8_t> bytes;
  void begin(unsigned long value) { baud = value; }
  bool connected() const { return isConnected; }
  void send_nb(uint8_t* data, uint32_t count, uint32_t* actual) {
    ++calls;
    *actual = isConnected ? std::min(count, writeLimit) : 0;
    bytes.insert(bytes.end(), data, data + *actual);
  }
};
extern TestSerial _SerialUSB;
