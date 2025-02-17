#pragma once
#include "config.h"
#include <stdint.h>

/// A simple line+column position
struct Pos {
  int8_t m_line;
  int8_t m_column;

  inline bool operator==(const Pos& other) const;
  inline bool operator!=(const Pos& other) const;
};

/// A single event press or release of a single key.
struct Event {
  /// Position of the key that was pressed or released.
  Pos m_pos;

  /// True if the key was pressed, false if it was released.
  bool m_isPressed;

  /// Timestamp of this event in micro-seconds
  unsigned long m_time;

#if DEBUG_LOG
  /// print the content of this event in the debug output.
  void print() const;
#endif
};

inline bool Pos::operator==(const Pos& other) const {
  return m_line == other.m_line && m_column == other.m_column;
}

inline bool Pos::operator!=(const Pos& other) const {
  return m_line != other.m_line || m_column != other.m_column;
}