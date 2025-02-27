#include "event.h"

#if DEBUG_LOG
void Event::print() const {
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

void EventQueue::print(const char* prefix) {
  for (Iterator it = begin(); it != end(); ++it) {
    const Item& item = m_events[it.m_index];
    debugPrint(prefix);
    debugPrint(it.m_index);
    debugPrint(" ");
    if (item.m_deleted) debugPrint("DELETED");
    item.m_item.print();
    debugPrintln();
  }
}
#endif