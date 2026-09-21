#include "trace.h"

#if REMOTE_TRACE
#include <Arduino.h>
#include "USB/PluggableUSBSerial.h"
#include <string.h>

namespace {
constexpr uint16_t FRAME_SIZE = 48;
constexpr uint16_t CAPACITY = 512;
constexpr uint32_t CHECKPOINT_INTERVAL_US = 2000000;
uint8_t frames[CAPACITY][FRAME_SIZE];
static_assert(sizeof(frames) == 24576, "Trace ring RAM budget changed");
uint16_t head = 0, size = 0, offset = 0;
uint32_t sequence = 0, dropped = 0, reportedDropped = 0, stream = 0;
uint32_t lastMicros = 0, checkpointTime = 0, checkpointId = 0;
uint64_t elapsed = 0;
bool connected = false;
uint32_t waitId = 0, waitRelated = 0;
uint8_t waitAction = 0;
const Trace::LayerName* layerNames = nullptr;
uint16_t layerCount = 0;

enum class Type : uint8_t {
  HELLO = 1, INPUT, REMOVE, DECISION, HID,
  SNAPSHOT_BEGIN, SNAPSHOT_ENTRY, SNAPSHOT_END, LOSS, I2C_RESET, QUEUE_OVERFLOW, HID_OUTPUT, LAYER_NAME
};

uint64_t now() {
  const uint32_t current = micros();
  elapsed += uint32_t(current - lastMicros);
  lastMicros = current;
  return elapsed;
}

void put16(uint8_t* out, uint16_t value) {
  out[0] = value;
  out[1] = value >> 8;
}

void put32(uint8_t* out, uint32_t value) {
  for (uint8_t i = 0; i < 4; ++i) out[i] = value >> (8 * i);
}

void emit(Type type, const uint8_t* payload, uint64_t time) {
  if (!connected) return;
  const uint32_t seq = ++sequence;
  if (size == CAPACITY) {
    ++dropped;
    return;
  }
  uint8_t* frame = frames[(head + size) % CAPACITY];
  memset(frame, 0, FRAME_SIZE);
  memcpy(frame, "KBT1", 4);
  frame[4] = uint8_t(type);
  put32(frame + 8, seq);
  put32(frame + 12, uint32_t(time));
  put32(frame + 16, uint32_t(time >> 32));
  memcpy(frame + 20, payload, 26);
  uint16_t crc = 0xffff;
  for (uint8_t i = 0; i < FRAME_SIZE - 2; ++i) {
    crc ^= uint16_t(frame[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
  }
  put16(frame + FRAME_SIZE - 2, crc);
  ++size;
}

void entry(Type type, const Event& event, uint8_t slot, uint64_t time) {
  uint8_t payload[26] = {};
  put32(payload, event.m_id);
  put32(payload + 4, uint32_t(event.m_time));
  payload[8] = event.m_pos.m_line;
  payload[9] = event.m_pos.m_column;
  payload[10] = event.m_isPressed;
  payload[11] = slot;
  emit(type, payload, time);
}

void drain() {
  // The Mbed Serial.write() is blocking. send_nb() copies only what fits.
  // Bound USB work per loop, retaining an unsent frame suffix for the next loop.
  for (uint8_t attempt = 0; attempt < 4 && size; ++attempt) {
    uint32_t actual = 0;
    _SerialUSB.send_nb(frames[head] + offset, FRAME_SIZE - offset, &actual);
    if (!actual) break;
    offset += actual;
    if (offset == FRAME_SIZE) {
      offset = 0;
      head = (head + 1) % CAPACITY;
      --size;
    }
  }
}

bool checkpoint(EventQueue& queue, uint64_t time) {
  uint16_t count = 0;
  for (auto it = queue.begin(); it != queue.end(); it = queue.next(it)) ++count;
  // This runs only on the scan thread. Reserve the entire batch before emitting
  // anything, so a slow reader cannot produce a partial logical checkpoint.
  if (CAPACITY - size < count + 3 + layerCount) return false;

  uint8_t hello[26] = {};
  hello[0] = VERSION;
  hello[1] = NUM_LINES;
  hello[2] = NUM_COLUMNS;
  hello[3] = OVERLAP_REMOVAL;
  put32(hello + 4, DEBOUNCE_TIME);
  put32(hello + 8, OVERLAP_REMOVAL_TIME);
  put32(hello + 12, MAX_HOLD_TIME);
  put32(hello + 16, KEY_PRESS_LENGTH);
  put32(hello + 20, stream);
  put16(hello + 24, layerCount);
  emit(Type::HELLO, hello, time);
  for (uint16_t mask = 0; mask < layerCount; ++mask) {
    uint8_t payload[26] = {};
    payload[0] = mask;
    payload[1] = strlen(layerNames[mask].text);
    memcpy(payload + 2, layerNames[mask].text, payload[1]);
    emit(Type::LAYER_NAME, payload, time);
  }

  uint8_t boundary[26] = {};
  put16(boundary, count);
  boundary[2] = queue.begin().m_index;
  boundary[3] = queue.end().m_index;
  put32(boundary + 4, ++checkpointId);
  emit(Type::SNAPSHOT_BEGIN, boundary, time);
  for (auto it = queue.begin(); it != queue.end(); it = queue.next(it)) {
    entry(Type::SNAPSHOT_ENTRY, queue[it], it.m_index, time);
  }
  emit(Type::SNAPSHOT_END, boundary, time);
  return true;
}
}

void Trace::begin(const LayerName* layers, uint16_t count) {
  layerNames = layers;
  layerCount = count;
  _SerialUSB.begin(115200);
  now();
}

void Trace::service(EventQueue& queue) {
  const uint64_t time = now();
  // Avoid USBSerial::operator bool(): it calls delay(1).
  if (!_SerialUSB.connected()) {
    connected = false;
    size = head = offset = 0;
    return;
  }
  if (!connected) {
    connected = true;
    ++stream;
    sequence = dropped = reportedDropped = 0;
    size = head = offset = 0;
    waitId = waitRelated = waitAction = 0;
    checkpoint(queue, time);
    checkpointTime = uint32_t(time);
  }
  drain();
  if (dropped != reportedDropped && size < CAPACITY) {
    uint8_t payload[26] = {};
    put32(payload, dropped);
    emit(Type::LOSS, payload, now());
    reportedDropped = dropped;
  }
  if (uint32_t(time - checkpointTime) >= CHECKPOINT_INTERVAL_US && checkpoint(queue, now())) {
    checkpointTime = uint32_t(time);
  }
}

void Trace::input(const Event& event, uint8_t slot) {
  if (connected) entry(Type::INPUT, event, slot, now());
}

void Trace::removed(const Event& event, uint8_t slot, Removal reason) {
  if (!connected) return;
  uint8_t payload[26] = {};
  put32(payload, event.m_id);
  payload[4] = uint8_t(reason);
  payload[5] = slot;
  emit(Type::REMOVE, payload, now());
  waitAction = 0;
}

void Trace::decision(const Event& event, Action action, uint32_t related, uint8_t layer, uint8_t count) {
  if (!connected) return;
  const bool waiting = action == Action::DEBOUNCE_WAIT || action == Action::OVERLAP_WAIT || action == Action::TAP_WAIT;
  if (waiting && waitId == event.m_id && waitRelated == related && waitAction == uint8_t(action)) return;
  waitId = event.m_id;
  waitRelated = related;
  waitAction = waiting ? uint8_t(action) : 0;
  uint8_t payload[26] = {};
  put32(payload, event.m_id);
  put32(payload + 4, related);
  payload[8] = uint8_t(action);
  payload[9] = layer;
  payload[10] = count;
  emit(Type::DECISION, payload, now());
}

void Trace::hid(const uint8_t* report, uint8_t length, bool success, uint32_t duration) {
  if (!connected) return;
  uint8_t payload[26] = {};
  payload[0] = length;
  payload[1] = success;
  put32(payload + 2, duration);
  memcpy(payload + 6, report, length);
  emit(Type::HID, payload, now());
}

void Trace::output(const uint8_t* keys, uint8_t modifiers, uint8_t media, bool keyboardSuccess, bool mediaSuccess, uint32_t keyboardDuration, uint32_t mediaDuration) {
  if (!connected) return;
  uint8_t payload[26] = {};
  payload[0] = keyboardSuccess;
  payload[1] = mediaSuccess;
  put32(payload + 2, keyboardDuration);
  put32(payload + 6, mediaDuration);
  payload[10] = 1;  // Keyboard report ID.
  payload[11] = modifiers;
  memcpy(payload + 13, keys, 6);
  payload[19] = 3;  // Media report ID.
  payload[20] = media;
  emit(Type::HID_OUTPUT, payload, now());
}

void Trace::i2cReset(uint8_t chip) {
  if (!connected) return;
  uint8_t payload[26] = {};
  payload[0] = chip;
  emit(Type::I2C_RESET, payload, now());
}

void Trace::queueOverflow(uint8_t queueHead, uint8_t queueTail) {
  if (!connected) return;
  uint8_t payload[26] = {};
  payload[0] = queueHead;
  payload[1] = queueTail;
  emit(Type::QUEUE_OVERFLOW, payload, now());
}
#endif
