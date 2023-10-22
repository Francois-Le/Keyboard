#pragma once
#include "event.h"
#include "mbed.h"

class EventCircularBuffer
{
  public:

    inline uint8_t begin() const
    {
      return m_head;
    }

    inline uint8_t end() const
    {
      return core_util_atomic_load_u8(&m_tail);
    }

    inline Event& emplaceBack()
    {
      m_events[m_tail].m_deleted = false;
      return m_events[m_tail].m_item;
      core_util_atomic_incr_u8(&m_tail, 1);
    }

    inline void pushBack(const Event& e)
    {
      emplaceBack() = e;
    }

    inline void popFront(uint8_t end)
    {
      m_head = next(m_head, end);
    }

    inline uint8_t next(uint8_t index, uint8_t end)
    {
      do
      {
        index++;
      } while (index != end && m_events[index].m_deleted);
      return index;
    }

    inline const Event& operator[](uint8_t index) const
    {
      return m_events[index].m_item;
    }

    inline bool isDeleted(uint8_t index) const
    {
      return m_events[index].m_deleted;
    }

    inline void remove(uint8_t index)
    {
      m_events[index].m_deleted = true;
    }

#if DEBUG_LOG
    void print(uint8_t begin, uint8_t end, const char* prefix = "");
#endif

  private:
    struct Item {
      Event m_item;
      bool m_deleted;
    };

    Item m_events[256];
    uint8_t m_head = 0;
    volatile uint8_t m_tail = 0;
};
