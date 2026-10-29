#include <cassert>
#include <cstdio>
#include "../arduino_keyboard/modifierStaging.h"

static const uint8_t RALT = 0x40;
static const uint8_t SHIFT = 0x02;
static const uint8_t KEY_7 = 0x24;
static const uint8_t KEY_A = 0x14;
static const uint8_t KEY_B = 0x05;

static bool equal(const uint8_t a[6], const uint8_t b[6]) {
  for (int i = 0; i < 6; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

int main() {
  const uint8_t none[6] = { 0, 0, 0, 0, 0, 0 };
  uint8_t out[6];

  // AltGr+7 from nothing: send AltGr alone first.
  {
    const uint8_t next[6] = { KEY_7, 0, 0, 0, 0, 0 };
    assert(stageModifiers(none, 0, next, RALT, out));
    assert(equal(out, none));
  }

  // Plain key without modifier change: no staging.
  {
    const uint8_t next[6] = { KEY_7, 0, 0, 0, 0, 0 };
    assert(!stageModifiers(none, 0, next, 0, out));
  }

  // Modifier changes but no new key (release AltGr+7): no staging.
  {
    const uint8_t prev[6] = { KEY_7, 0, 0, 0, 0, 0 };
    assert(!stageModifiers(prev, RALT, none, 0, out));
  }

  // Modifier only (Shift alone): no staging.
  assert(!stageModifiers(none, 0, none, SHIFT, out));

  // Keys still held stay in the intermediate report; the new key waits.
  {
    const uint8_t prev[6] = { KEY_A, 0, 0, 0, 0, 0 };
    const uint8_t next[6] = { KEY_A, KEY_B, 0, 0, 0, 0 };
    const uint8_t expected[6] = { KEY_A, 0, 0, 0, 0, 0 };
    assert(stageModifiers(prev, 0, next, SHIFT, out));
    assert(equal(out, expected));
  }

  // Modifier released and new key pressed: release the modifier and the old key first.
  {
    const uint8_t prev[6] = { KEY_A, 0, 0, 0, 0, 0 };
    const uint8_t next[6] = { KEY_B, 0, 0, 0, 0, 0 };
    assert(stageModifiers(prev, SHIFT, next, 0, out));
    assert(equal(out, none));
  }

  std::puts("modifier staging tests passed");
  return 0;
}
