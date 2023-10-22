#include "eventQueue.h"
#include "debug.h"

#if DEBUG_LOG
void EventCircularBuffer::print(uint8_t begin, uint8_t end, const char* prefix)
{
  for (uint8_t it = begin; it != end; ++it)
  {
    const Item& item = m_events[it];
    debugPrint(prefix);
    debugPrint(it);
    debugPrint(" ");
    if (item.m_deleted) debugPrint("DELETED");
    item.m_item.print();
    debugPrintln();
  }
}
#endif
