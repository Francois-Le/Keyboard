#include "event.h"

#if DEBUG_LOG
void Event::print() const
{
  debugPrint("Event(line: ");
  debugPrint(m_pos.m_line);
  debugPrint("\tcolumn: ");
  debugPrint(m_pos.m_column);
  debugPrint("\tisPressed: ");
  debugPrint(m_isPressed);
  debugPrint("\ttime: ");
  debugPrint(m_time);
  debugPrint(")");
}
#endif
