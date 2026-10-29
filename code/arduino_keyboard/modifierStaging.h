#pragma once
#include "stdint.h"

/// Some hosts (for example the Remote Desktop client) do not keep the order of a modifier and a key that change in the
/// same HID report. They can send the key before the modifier, so AltGr+7 becomes "7". To prevent this, a report that
/// both changes the modifiers and presses a new key is split: first a report with the new modifiers and only the keys
/// that stay pressed, then the full report.
///
/// Returns true if an intermediate report must be sent before 'nextKeys'. In this case 'outKeys' receives its keys.
inline bool stageModifiers(const uint8_t prevKeys[6], uint8_t prevModifiers, const uint8_t nextKeys[6], uint8_t nextModifiers, uint8_t outKeys[6]) {
  if (prevModifiers == nextModifiers) return false;

  auto contains = [](const uint8_t keys[6], uint8_t k) {
    for (int i = 0; i < 6; ++i) {
      if (keys[i] == k) return true;
    }
    return false;
  };

  bool anyNewKey = false;
  int count = 0;
  for (int i = 0; i < 6; ++i) outKeys[i] = 0;
  for (int i = 0; i < 6; ++i) {
    const uint8_t k = nextKeys[i];
    if (k == 0) continue;
    if (contains(prevKeys, k)) {
      outKeys[count++] = k;
    } else {
      anyNewKey = true;
    }
  }
  return anyNewKey;
}
