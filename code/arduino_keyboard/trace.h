#pragma once
#include "config.h"
#include "event.h"

namespace Trace {
enum class Removal : uint8_t { PROCESSED = 1, DEBOUNCE, TAP, OVERLAP };
enum class Action : uint8_t {
  DEBOUNCE_WAIT = 1, DEBOUNCE_CANCEL, OVERLAP_WAIT, OVERLAP_TAP,
  TAP_WAIT, TAP, HOLD, PROCESS, LAYER
};

#if REMOTE_TRACE
void begin();
void service(EventQueue& queue);
void input(const Event& event, uint8_t slot);
void removed(const Event& event, uint8_t slot, Removal reason);
void decision(const Event& event, Action action, uint32_t related = 0, uint8_t layer = 0, uint8_t count = 0);
void hid(const uint8_t* report, uint8_t length, bool success, uint32_t duration);
void i2cReset(uint8_t chip);
void queueOverflow(uint8_t head, uint8_t tail);
#else
inline void begin() {}
inline void service(EventQueue&) {}
inline void input(const Event&, uint8_t) {}
inline void removed(const Event&, uint8_t, Removal) {}
inline void decision(const Event&, Action, uint32_t = 0, uint8_t = 0, uint8_t = 0) {}
inline void hid(const uint8_t*, uint8_t, bool, uint32_t) {}
inline void i2cReset(uint8_t) {}
inline void queueOverflow(uint8_t, uint8_t) {}
#endif
}
