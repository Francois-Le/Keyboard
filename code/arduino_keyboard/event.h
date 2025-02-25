#pragma once
#include "config.h"
#include "pos.h"
#include <stdint.h>

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


/// FIFO queue for event. Support arbitrary removals.
///
/// To support abitrary removal without memory copy we mark event that are deleted but leave them in the queue. The API of the queue will guarantee to never return them.
class EventQueue {
public:
  struct Iterator {
    uint8_t m_index;

    inline bool operator==(Iterator other) const;
    inline bool operator!=(Iterator other) const;
    inline Iterator& operator++();
    inline Iterator operator*() const;
  };

  /// Insert a new event at the end of queue and return a mutable reference to it.
  inline Event& emplaceBack();

  /// Add an event at the end of the queue.
  inline void pushBack(const Event& e);

  /// True if the queue is empty.
  inline bool isEmpty() const;

  /// Loot at the first event in the queue without consumint it.
  inline const Event& peek() const;

  /// Consume the first event in the queue.
  inline void popFront();

  /// Iterator to the first event in the queue.
  inline Iterator begin() const;

  /// Gicen an iterator index, return an iterator to the next event.
  inline Iterator next(Iterator it);

  /// Iterator to the end of the queue.
  inline Iterator end() const;

  /// Access an event in the queue with an iterator.
  inline const Event& operator[](Iterator it) const;

  /// Access an event in the queue with an iterator.
  inline Event& operator[](Iterator it);

  /// Remove the event at the iterator index in the queue. This does not need to be the first event in the queue. The events in the queue are not re-ordered.
  inline void remove(Iterator it);

#if DEBUG_LOG
  /// print the content of this event queue in the debug output.
  void print(const char* prefix = "");
#endif

private:
  /// An entry in the circular buffer
  struct Item {
    /// the value of the event for this entry.
    Event m_item;

    /// True if this event is deleted.
    ///
    /// This is set by EventQueue::remove(). Deleted entries are skiped when iterating on events in the queue.
    bool m_deleted;
  };

  /// list of event in this queue.
  Item m_events[256];

  /// Index of the front of the queue.
  Iterator m_head{ 0 };

  /// index of the element after the last in the queue.
  Iterator m_tail{ 0 };
};

// BELOW IS IMPLEMENTATION OF INLINE FUNCTIONS

inline bool EventQueue::Iterator::operator==(Iterator other) const {
  return m_index == other.m_index;
}

inline bool EventQueue::Iterator::operator!=(Iterator other) const {
  return m_index != other.m_index;
}

inline EventQueue::Iterator& EventQueue::Iterator::operator++() {
  ++m_index;
  return *this;
}

inline EventQueue::Iterator EventQueue::Iterator::operator*() const {
  return *this;
}

inline Event& EventQueue::emplaceBack() {
  m_events[m_tail.m_index].m_deleted = false;
  return m_events[m_tail.m_index++].m_item;
}

inline void EventQueue::pushBack(const Event& e) {
  emplaceBack() = e;
}

inline bool EventQueue::isEmpty() const {
  return m_head == m_tail;
}

inline const Event& EventQueue::peek() const {
  return m_events[m_head.m_index].m_item;
}

inline void EventQueue::popFront() {
  m_head = next(m_head);
}

inline EventQueue::Iterator EventQueue::begin() const {
  return m_head;
}

inline EventQueue::Iterator EventQueue::next(Iterator it) {
  do {
    it.m_index++;
  } while (it != m_tail && m_events[it.m_index].m_deleted);
  return it;
}

inline EventQueue::Iterator EventQueue::end() const {
  return m_tail;
}

inline const Event& EventQueue::operator[](Iterator it) const {
  return m_events[it.m_index].m_item;
}

inline Event& EventQueue::operator[](Iterator it) {
  return m_events[it.m_index].m_item;
}

inline void EventQueue::remove(Iterator it) {
  if (it == m_head) {
    m_head.m_index++;
  } else {
    m_events[it.m_index].m_deleted = true;
  }
}