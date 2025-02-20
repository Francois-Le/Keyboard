#pragma once
#include <stdint.h>

/// A simple line+column position
struct Pos {
  int8_t m_line;
  int8_t m_column;

  inline bool operator==(const Pos& other) const;
  inline bool operator!=(const Pos& other) const;
};

// BELOW IS IMPLEMENTATION OF INLINE FUNCTIONS

inline bool Pos::operator==(const Pos& other) const {
  return m_line == other.m_line && m_column == other.m_column;
}

inline bool Pos::operator!=(const Pos& other) const {
  return m_line != other.m_line || m_column != other.m_column;
}