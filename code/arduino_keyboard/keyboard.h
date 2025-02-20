#pragma once
#include "stdint.h"

enum ModifierKeys {
  MOD_CTRL = 0x01,
  MOD_SHIFT = 0x02,
  MOD_ALT = 0x04,
  MOD_WIN = 0x08,
  MOD_RCTRL = 0x10,
  MOD_RSHIFT = 0x20,
  MOD_RALT = 0x40,
  MOD_RWIN = 0x80,
};

enum class Key {
  NONE = 0,

  A = 0x14,
  B = 0x05,
  C = 0x06,
  D = 0x07,
  E = 0x08,
  F = 0x09,
  G = 0x0A,
  H = 0x0B,
  I = 0x0C,
  J = 0x0D,
  K = 0x0E,
  L = 0x0F,
  M = 0x33,
  N = 0x11,
  O = 0x12,
  P = 0x13,
  Q = 0x04,
  R = 0x15,
  S = 0x16,
  T = 0x17,
  U = 0x18,
  V = 0x19,
  W = 0x1D,
  X = 0x1B,
  Y = 0x1C,
  Z = 0x1A,

  P1 = 0x2F,
  P2 = 0x30,
  M1 = 0x34,
  M2 = 0x32,
  N1 = 0x10,
  N2 = 0x36,
  N3 = 0x37,
  N4 = 0x38,
  W1 = 0x64,

  POW_2 = 0x35,
  D1 = 0x1E,
  D2 = 0x1F,
  D3 = 0x20,
  D4 = 0x21,
  D5 = 0x22,
  D6 = 0x23,
  D7 = 0x24,
  D8 = 0x25,
  D9 = 0x26,
  D0 = 0x27,
  D01 = 0x2D,
  D02 = 0x2E,

  ESC = 0x29,
  F1 = 0x3A,
  F2 = 0x3B,
  F3 = 0x3C,
  F4 = 0x3D,
  F5 = 0x3E,
  F6 = 0x3F,
  F7 = 0x40,
  F8 = 0x41,
  F9 = 0x42,
  F10 = 0x43,
  F11 = 0x44,
  F12 = 0x45,

  DEL = 0x4C,
  INS = 0x49,
  END = 0x4D,
  HOME = 0x4A,
  P_UP = 0x4B,
  P_DOWN = 0x4E,
  PRI_SCR = 0x46,
  LOCK_SC = 0x47,
  PAUSE = 0x48,

  NUM_0 = 0x62,
  NUM_1 = 0x59,
  NUM_2 = 0x5A,
  NUM_3 = 0x5B,
  NUM_4 = 0x5C,
  NUM_5 = 0x5D,
  NUM_6 = 0x5E,
  NUM_7 = 0x5F,
  NUM_8 = 0x60,
  NUM_9 = 0x61,
  NUM_ENTER = 0x58,
  NUM_PLUS = 0x57,
  NUM_MINUS = 0x56,
  NUM_MUL = 0x55,
  NUM_DIV = 0x54,
  NUM_LOCK = 0x53,

  UP = 0x52,
  DOWN = 0x51,
  LEFT = 0x50,
  RIGHT = 0x4F,

  CAP_LOCK = 0x39,
  TAB = 0x2B,
  SPACE = 0x2C,
  MENU = 0x65,
  ENTER = 0x28,
  BACKSPACE = 0x2A,

  CTRL = 0xE0,
  SHIFT = 0xE1,
  ALT = 0xE2,
  WIN = 0xE3,
  RCTRL = 0xE4,
  RSHIFT = 0xE5,
  RALT = 0xE6,
  RWIN = 0xE7,
};

enum class MediaKey {
  NONE = 0,

  VOLUME_UP = 0x20,
  VOLUME_DOWN = 0x40,
};

class KeyboardOutput {
public:
  inline void add(Key k);

  inline void add(MediaKey k);

  void send();

  inline bool isAnyKeyPressed();

#if DEBUG_LOG
  void print() const;
#endif

private:
  uint8_t m_modifiers = 0;
  uint8_t m_keys[6] = { 0, 0, 0, 0, 0, 0 };
  int m_nextKey = 0;
  uint8_t m_mediaKey = 0;
};

// BELOW IS IMPLEMENTATION OF INLINE FUNCTIONS

inline void KeyboardOutput::add(Key k) {
  switch (k) {
    case Key::CTRL: m_modifiers = m_modifiers | MOD_CTRL; break;
    case Key::SHIFT: m_modifiers = m_modifiers | MOD_SHIFT; break;
    case Key::ALT: m_modifiers = m_modifiers | MOD_ALT; break;
    case Key::WIN: m_modifiers = m_modifiers | MOD_WIN; break;
    case Key::RCTRL: m_modifiers = m_modifiers | MOD_RCTRL; break;
    case Key::RSHIFT: m_modifiers = m_modifiers | MOD_RSHIFT; break;
    case Key::RALT: m_modifiers = m_modifiers | MOD_RALT; break;
    case Key::RWIN: m_modifiers = m_modifiers | MOD_RWIN; break;

    default:
      if (m_nextKey < 6) {
        m_keys[m_nextKey] = uint8_t(k);
        m_nextKey++;
      }
      break;
  }
}

inline void KeyboardOutput::add(MediaKey k) {
  m_mediaKey = uint8_t(k);
}

inline bool KeyboardOutput::isAnyKeyPressed() {
  return m_modifiers != 0 || m_nextKey != 0;
}
