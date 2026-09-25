#pragma once
#include "config.h"
#include "event.h"

namespace Trace {
struct LayerName {
  char text[25];
};
constexpr bool validLayerNames(const LayerName* layers, uint16_t count) {
  for (uint16_t i = 0; i < count; ++i) {
    uint8_t length = 0;
    while (length < 25 && layers[i].text[length]) {
      if (layers[i].text[length] < 32 || layers[i].text[length] > 126) return false;
      ++length;
    }
    if (length == 0 || length > 24) return false;
  }
  return true;
}
enum class Removal : uint8_t { PROCESSED = 1, DEBOUNCE, TAP, OVERLAP };
enum class Action : uint8_t {
  DEBOUNCE_WAIT = 1, DEBOUNCE_CANCEL, OVERLAP_WAIT, OVERLAP_TAP,
  TAP_WAIT, TAP, HOLD, PROCESS, LAYER
};

#if REMOTE_TRACE
void begin(const LayerName* layers, uint16_t count);
void service(EventQueue& queue);
void input(const Event& event, uint8_t slot);
void removed(const Event& event, uint8_t slot, Removal reason);
void decision(const Event& event, Action action, uint32_t related = 0, uint8_t layer = 0, uint8_t count = 0);
void hid(const uint8_t* report, uint8_t length, bool success, uint32_t duration);
void output(const uint8_t* keys, uint8_t modifiers, uint8_t media, bool keyboardSuccess, bool mediaSuccess, uint32_t keyboardDuration, uint32_t mediaDuration);
void i2cReset(uint8_t chip);
void queueOverflow(uint8_t head, uint8_t tail);
#else
inline void begin(const LayerName*, uint16_t) {}
inline void service(EventQueue&) {}
inline void input(const Event&, uint8_t) {}
inline void removed(const Event&, uint8_t, Removal) {}
inline void decision(const Event&, Action, uint32_t = 0, uint8_t = 0, uint8_t = 0) {}
inline void hid(const uint8_t*, uint8_t, bool, uint32_t) {}
inline void output(const uint8_t*, uint8_t, uint8_t, bool, bool, uint32_t, uint32_t) {}
inline void i2cReset(uint8_t) {}
inline void queueOverflow(uint8_t, uint8_t) {}
#endif
}
