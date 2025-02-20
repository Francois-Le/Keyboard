#include "mbed.h"
#include "config.h"
#include "input.h"
#include "PluggableUSBHID.h"
#include "Keyboard.h"
#include "KeyConfig.h"
#include "event.h"



class LayerTracker {
public:
  inline void delta(LayerBit i, uint8_t d) {
    if (i > 0) {
      m_count[i - 1] += d;

      // Update mask.
      m_mask = 0;
      for (uint8_t i = 0, p = 1; i < 8; ++i, p = p << 1) {
        m_mask = m_mask | (m_count[i] > 0 ? p : 0);
      }
    }
  }

  inline uint8_t mask() {
    return m_mask;
  }

private:
  uint8_t m_count[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t m_mask = 0;
};

uint8_t s_currentPressCount[NUM_LINES][NUM_COLUMNS];

EventQueue s_events;
LayerTracker s_layerTracker;

inline void addK(const K& keys, KeyboardOutput& output) {
  if (keys.m_key0 != Key::NONE) output.add(keys.m_key0);
  if (keys.m_key1 != Key::NONE) output.add(keys.m_key1);
  if (keys.m_mediaKey != MediaKey::NONE) output.add(keys.m_mediaKey);
}

Key s_forcedKey = Key::NONE;
void fillCurrentKeyPress(KeyboardOutput& output);
void fillCurrentKeyPress(KeyboardOutput& output) {
  K(*currentLayer)
  [12] = s_keyMaps[s_layerTracker.mask()];
  debugPrint("\tLayer mask: ");
  debugPrintln(s_layerTracker.mask());

  for (int line = 0; line < NUM_LINES; ++line) {
    for (int column = 0; column < NUM_COLUMNS; ++column) {
      if (s_currentPressCount[line][column] > 0) {
        K key = currentLayer[line][column];
        addK(key, output);
      }
    }
  }

  if (!output.isAnyKeyPressed() && s_layerTracker.mask() == 1) {
    addK(Key::SHIFT, output);
    debugPrintln("\tAdding shift");
  }

  if (s_forcedKey != Key::NONE) {
    addK(s_forcedKey, output);
  }
}



void setup() {
#if ANY_LOG
  Serial.begin(9600);
#endif

  Input::init();

  for (int line = 0; line < NUM_LINES; ++line) {
    for (int column = 0; column < NUM_COLUMNS; ++column) {
      s_currentPressCount[line][column] = 0;
    }
  }
}

#if PERF_LOG
unsigned long perf_min = -1;
unsigned long perf_max = 0;
unsigned long perf_total = 0;
int perf_count = 0;
static const int perf_countTotal = 1000;
#endif

void loop() {
#if PERF_LOG
  unsigned long perf_start = micros();
#endif

  Input::step();

  ON_DEBUG(bool anyNewEvent = false);
  for (int line = 0; line < NUM_LINES; ++line) {
    for (int column = 0; column < NUM_COLUMNS; ++column) {

      if (Input::hasChanged(line, column)) {
        Event& event = s_events.emplaceBack();
        event.m_pos = Pos{ line, column };
        event.m_isPressed = Input::isPressed(line, column);
        event.m_time = micros();

        ON_DEBUG(anyNewEvent = true);
      }
    }
  }

#if DEBUG_LOG
  if (anyNewEvent) {
    debugPrintln("new events added to queue:");
    s_events.print("\t");
  }
#endif

  unsigned long current = micros();
  while (!s_events.isEmpty()) {
    const Event& event = s_events.peek();

    // First do debouncing.
    {
      if (current - event.m_time < DEBOUNCE_TIME) break;  // We have to wait a bit and pull the switches to check if we need to debounce.

      {
        bool debounced = false;
        for (EventQueue::Iterator it : s_events) {
          if (s_events[it].m_time - event.m_time > DEBOUNCE_TIME) {
            break;
          }

          if (s_events[it].m_pos == event.m_pos && s_events[it].m_isPressed != event.m_isPressed) {
            debugPrint("Cancel key\n");
            s_events.popFront();
            s_events.remove(it);
            debounced = true;
            break;
          }
        }

#if DEBUG_LOG
        if (debounced) {
          debugPrintln("Debounced a key, event queue:");
          s_events.print("\t");
        }
#endif

        if (debounced) continue;  // We debounced the current event, go to next.
      }
    }
    if (onRelease[event.m_pos.m_line][event.m_pos.m_column] && event.m_isPressed) {
      bool foundRelease = false;
      bool foundAnotherPress = false;
      EventQueue::Iterator releaseIndex;
      for (EventQueue::Iterator it : s_events) {
        if (s_events[it].m_pos == event.m_pos && !s_events[it].m_isPressed) {
          foundRelease = true;
          releaseIndex = it;
          break;
        }

        if (s_events[it].m_pos != event.m_pos && s_events[it].m_isPressed) {
          foundAnotherPress = true;
          break;
        }
      }

      if (!foundAnotherPress) {
        if (foundRelease) {
          if (s_events[releaseIndex].m_time - event.m_time < MAX_HOLD_TIME) {
            debugPrintln("Press and release key");
            s_currentPressCount[event.m_pos.m_line][event.m_pos.m_column]++;

            {
              KeyboardOutput output;
              fillCurrentKeyPress(output);
              output.send();

              debugPrint("\tSending event: ");
              ON_DEBUG(output.print());
              debugPrintln();
            }

            s_currentPressCount[event.m_pos.m_line][event.m_pos.m_column]--;
            delay(KEY_PRESS_LENGTH);  // milliseconds

            {
              KeyboardOutput output;
              fillCurrentKeyPress(output);
              output.send();

              debugPrint("\tSending event: ");
              ON_DEBUG(output.print());
              debugPrintln();
            }
          }
          s_events.popFront();
          s_events.remove(releaseIndex);
        }

        break;
      }
    }

#if DEBUG_LOG
    debugPrint("processing event ");
    debugPrint(s_events.begin());
    debugPrint(" ");
    event.print();
    debugPrintln();
#endif

    // Update the press count of each button.
    uint8_t& pressCount = s_currentPressCount[event.m_pos.m_line][event.m_pos.m_column];
    if (event.m_isPressed) {
      pressCount++;
    } else {
      if (pressCount > 0) pressCount--;
    }

    // Update forced key.
    {
      K(*currentLayer)
      [12] = s_keyMaps[s_layerTracker.mask()];
      Key forced = currentLayer[event.m_pos.m_line][event.m_pos.m_column].m_forcedKey;
      if (forced != Key::NONE) {
        s_forcedKey = forced;
      }
    }

    // Update the the layer (if any change).
    LayerBit layer = s_layerKeys[event.m_pos.m_line][event.m_pos.m_column];
    if (layer != LAYER_NONE) {
      s_layerTracker.delta(layer, event.m_isPressed ? +1 : -1);

      // Reset button presses when layer change.
      for (int line = 0; line < NUM_LINES; ++line) {
        for (int column = 0; column < NUM_COLUMNS; ++column) {
          if (immuneToReset[line][column]) continue;
          s_currentPressCount[line][column] = 0;
        }
      }

      s_forcedKey = Key::NONE;
    }

#ifdef DEBUG_LOG
    debugPrintln("Current press count:");
    for (int line = 0; line < NUM_LINES; ++line) {
      debugPrint("\t");
      for (int column = 0; column < NUM_COLUMNS; ++column) {
        if (column == 6) debugPrint(" ");
        debugPrint(s_currentPressCount[line][column]);
      }
      debugPrintln();
    }
#endif

    // Send an event.
    {
      debugPrintln("Send event:");

      KeyboardOutput output;
      fillCurrentKeyPress(output);
      output.send();

      debugPrint("\tSending event: ");
      ON_DEBUG(output.print());
      debugPrintln();
    }

    s_events.popFront();
  }

#if PERF_LOG
  unsigned long total = micros() - perf_start;

  perf_min = min(perf_min, total);
  perf_max = max(perf_max, total);
  perf_total += total;
  perf_count++;
  if (perf_count == perf_countTotal) {
    unsigned long avg = perf_total / perf_countTotal;
    Serial.print("pulling perf min=");
    Serial.print(perf_min);
    Serial.print("us max=");
    Serial.print(perf_max);
    Serial.print("us avg=");
    Serial.print(avg);
    Serial.println("us");

    perf_min = -1;
    perf_max = 0;
    perf_total = 0;
    perf_count = 0;
  }
#endif
}
