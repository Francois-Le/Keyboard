#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include "../arduino_keyboard/trace.cpp"

uint32_t testMicros = 0;
TestSerial _SerialUSB;

static uint32_t u32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

static uint64_t u64(const uint8_t* p) {
  return u32(p) | uint64_t(u32(p + 4)) << 32;
}

static void drainAll(EventQueue& queue) {
  for (int i = 0; i < 3000; ++i) Trace::service(queue);
}

static void validateFrames() {
  assert(_SerialUSB.bytes.size() % 48 == 0);
  for (size_t i = 0; i < _SerialUSB.bytes.size(); i += 48) {
    const uint8_t* p = _SerialUSB.bytes.data() + i;
    assert(memcmp(p, "KBT1", 4) == 0);
    uint16_t crc = 0xffff;
    for (size_t j = 0; j < 46; ++j) {
      crc ^= uint16_t(p[j]) << 8;
      for (int bit = 0; bit < 8; ++bit) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    assert(crc == (uint16_t(p[46]) | uint16_t(p[47]) << 8));
  }
}

static Event& enqueue(EventQueue& queue, uint32_t id) {
  const uint8_t slot = queue.end().m_index;
  Event& event = queue.emplaceBack();
  event.m_id = id;
  event.m_time = testMicros;
  event.m_pos = Pos{1, 2};
  event.m_isPressed = (id % 2) != 0;
  Trace::input(event, slot);
  return event;
}

int main(int argc, char** argv) {
  EventQueue queue;
  const Trace::LayerName names[] = {
    {"Base"}, {"Shift"}, {"Function"}, {"Function"},
    {"Accent"}, {"Accent"}, {"Accent 2"}, {"Accent 2"}
  };
  constexpr size_t initialFrames = 3 + 8;
  testMicros = 0xfffffff0;
  Trace::begin(names, 8);
  assert(_SerialUSB.baud == 115200);
  Trace::service(queue);
  assert(_SerialUSB.bytes.empty());

  _SerialUSB.isConnected = true;
  _SerialUSB.writeLimit = 7;
  Trace::service(queue);
  assert(_SerialUSB.calls <= 4);
  drainAll(queue);
  validateFrames();
  assert(_SerialUSB.bytes.size() == initialFrames * 48);
  assert(_SerialUSB.bytes[4] == 1);
  assert(_SerialUSB.bytes[44] == 8 && _SerialUSB.bytes[45] == 0);
  for (size_t mask = 0; mask < 8; ++mask) {
    const uint8_t* entry = _SerialUSB.bytes.data() + (mask + 1) * 48;
    assert(entry[4] == 13 && entry[20] == mask);
    assert(entry[21] == strlen(names[mask].text));
    assert(memcmp(entry + 22, names[mask].text, entry[21]) == 0);
  }
  assert(_SerialUSB.bytes[(initialFrames - 2) * 48 + 4] == 6);
  assert(_SerialUSB.bytes[(initialFrames - 1) * 48 + 4] == 8);

  testMicros = 0x20;
  Event& input = enqueue(queue, 1);
  Trace::decision(input, Trace::Action::DEBOUNCE_WAIT);
  Trace::decision(input, Trace::Action::DEBOUNCE_WAIT);
  drainAll(queue);
  validateFrames();
  assert(_SerialUSB.bytes.size() == (initialFrames + 2) * 48);
  assert(u64(_SerialUSB.bytes.data() + initialFrames * 48 + 12) == 0x100000020ULL);
  assert(u32(_SerialUSB.bytes.data() + initialFrames * 48 + 20) == 1);

  Trace::removed(input, queue.begin().m_index, Trace::Removal::PROCESSED);
  queue.popFront();
  const uint8_t report[] = {1, 4, 0, 4, 24, 0, 0, 0, 0};
  Trace::output(report + 3, report[1], 0x20, true, false, 15, 19);
  drainAll(queue);
  assert(_SerialUSB.bytes.size() == (initialFrames + 4) * 48);
  const uint8_t* output = _SerialUSB.bytes.data() + (initialFrames + 3) * 48;
  assert(output[4] == 12);
  assert(output[20] == 1 && output[21] == 0);
  assert(u32(output + 22) == 15 && u32(output + 26) == 19);
  assert(memcmp(output + 30, report, sizeof(report)) == 0);
  assert(output[39] == 3 && output[40] == 0x20);

  // Force physical slot wrap and a tombstone without altering the trace queue.
  for (int i = 1; i < 254; ++i) {
    queue.emplaceBack();
    queue.popFront();
  }
  enqueue(queue, 2);
  enqueue(queue, 3);
  enqueue(queue, 4);
  Trace::removed(queue[EventQueue::Iterator{255}], 255, Trace::Removal::DEBOUNCE);
  queue.remove(EventQueue::Iterator{255});
  testMicros += 2000000;
  drainAll(queue);
  validateFrames();
  const size_t length = _SerialUSB.bytes.size();
  assert(_SerialUSB.bytes[length - (initialFrames + 2) * 48 + 4] == 1);
  assert(_SerialUSB.bytes[length - 4 * 48 + 4] == 6);
  assert(_SerialUSB.bytes[length - 3 * 48 + 20 + 11] == 254);
  assert(_SerialUSB.bytes[length - 2 * 48 + 20 + 11] == 0);

  _SerialUSB.writeLimit = 0;
  for (int i = 0; i < 700; ++i) Trace::i2cReset(2);
  const uint32_t beforeCalls = _SerialUSB.calls;
  Trace::service(queue);
  assert(_SerialUSB.calls == beforeCalls + 1);
  _SerialUSB.writeLimit = 5;
  drainAll(queue);
  validateFrames();
  bool sawLoss = false, sawGap = false;
  uint32_t previous = 0;
  for (size_t i = 0; i < _SerialUSB.bytes.size(); i += 48) {
    const uint8_t* p = _SerialUSB.bytes.data() + i;
    if (previous && u32(p + 8) != previous + 1) sawGap = true;
    previous = u32(p + 8);
    if (p[4] == 9) {
      sawLoss = true;
      assert(u32(p + 20) == 188);
    }
  }
  assert(sawLoss && sawGap);
  testMicros += 2000000;
  drainAll(queue);
  validateFrames();
  assert(_SerialUSB.bytes[_SerialUSB.bytes.size() - 48 + 4] == 8);

  Trace::decision(queue.peek(), Trace::Action::LAYER, 0, 6);
  drainAll(queue);
  validateFrames();
  assert(_SerialUSB.bytes[_SerialUSB.bytes.size() - 48 + 4] == 4);
  assert(_SerialUSB.bytes[_SerialUSB.bytes.size() - 48 + 29] == 6);

  if (argc == 2) {
    std::ofstream output(argv[1], std::ios::binary);
    output.write(reinterpret_cast<const char*>(_SerialUSB.bytes.data()), _SerialUSB.bytes.size());
    assert(output.good());
  }

  // Disconnect in the middle of a frame; reconnect starts a fresh stream.
  _SerialUSB.bytes.clear();
  Trace::i2cReset(1);
  _SerialUSB.writeLimit = 1;
  Trace::service(queue);
  assert(_SerialUSB.bytes.size() == 4);
  _SerialUSB.isConnected = false;
  Trace::service(queue);
  _SerialUSB.bytes.clear();
  _SerialUSB.isConnected = true;
  _SerialUSB.writeLimit = 48;
  drainAll(queue);
  validateFrames();
  assert(_SerialUSB.bytes[4] == 1);
  assert(u32(_SerialUSB.bytes.data() + 8) == 1);
  assert(u32(_SerialUSB.bytes.data() + 20 + 20) == 2);
  std::puts("Firmware trace: partial writes, CRC, clock wrap, queue checkpoint, drops and reconnect passed.");
}
