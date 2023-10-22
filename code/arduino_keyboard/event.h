#pragma once
#include "arduino.h"
#include "debug.h"

struct Pos
{
  int8_t m_line;
  int8_t m_column;
};

inline bool operator==(const Pos& a, const Pos& b)
{
  return a.m_line == b.m_line && a.m_column == b.m_column;
}

inline bool operator!=(const Pos& a, const Pos& b)
{
  return a.m_line != b.m_line || a.m_column != b.m_column;
}

struct Event
{
  Pos m_pos;
  bool m_isPressed;
  unsigned long m_time;

#if DEBUG_LOG
  void print() const;
#endif
};
