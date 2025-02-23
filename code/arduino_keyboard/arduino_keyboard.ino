#include "mbed.h"
#include "config.h"
#include "input.h"
#include "PluggableUSBHID.h"
#include "Keyboard.h"
#include "KeyConfig.h"
#include "event.h"



class LayerTracker {
public:
  inline void delta(LayerBit i, int8_t d) {
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
  int8_t m_count[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t m_mask = 0;
};

/// The queue of input event that are yet to be processed.
EventQueue s_events;

/// For each switch on the physical keyboard, track if that switch is currently pressed.
/// Instead of being a bool, we use a counter where "count = 0" mean the switch is not pressed and "count > 0" mean the switch is pressed. We do that so we can do some tricks where we force enable some of the switches momentarely by adding 1 without having to worry about if the switch was already pressed.
uint8_t s_currentPressCount[NUM_LINES][NUM_COLUMNS];

/// Track what layer is currently active
LayerTracker s_layerTracker;

/// Additional key that is forced to be in the output even if the switch is causing that key to be held. This is reset on layer changes.
Key s_forcedKey = Key::NONE;

/// Add 'keys' to the output.
inline void addK(const K& keys) {
  if (keys.m_key0 != Key::NONE) KeyboardOutput::add(keys.m_key0);
  if (keys.m_key1 != Key::NONE) KeyboardOutput::add(keys.m_key1);
  if (keys.m_mediaKey != MediaKey::NONE) KeyboardOutput::add(keys.m_mediaKey);
}

/// Send all the key pressed to the USB bus given the current state of the keyboard.
void sendCurrentKeyPress();
void sendCurrentKeyPress() {
  // Start by resetting the output.
  KeyboardOutput::releaseAll();

  // Get the switch to key mapping for the current active layer.
  K(*currentLayer)
  [12] = s_keyMaps[s_layerTracker.mask()];
  debugPrint("\tLayer mask: ");
  debugPrintln(s_layerTracker.mask());

  // Add the key associated to every active switch.
  for (int line = 0; line < NUM_LINES; ++line) {
    for (int column = 0; column < NUM_COLUMNS; ++column) {
      if (s_currentPressCount[line][column] > 0) {
        K key = currentLayer[line][column];
        addK(key);
      }
    }
  }

  // Add any key that are currently forced held.
  if (s_forcedKey != Key::NONE) {
    addK(s_forcedKey);
  }

  // HACK: If layer 1 is enabled and no key is currently pressed, we press shift. This is because physical shift key is associated to a layer, but if we are not pressing any key on that layer we want shift to be pressed.
  // TODO: improve this code to be more generic.
  if (!KeyboardOutput::isAnyKeyPressed() && s_layerTracker.mask() == 1) {
    addK(Key::SHIFT);
    debugPrintln("\tAdding shift");
  }

  // Send to the USB bus.
  KeyboardOutput::send();
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

  // We refresh the input.
  Input::step();

  // We add an event in the event queue for each key that have changed state.
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

  // Now we are going to process the every event in the queue that we can.
  unsigned long current = micros();
  while (!s_events.isEmpty()) {
    const Event& event = s_events.peek();

    // First we take care of debouncing.
    // The way we do debouncing is we wait any event to be older than DEBOUNCE_TIME before processing to check if we can cancel it.
    {
      // If the event is not old enough, we have to wait a bit and pull the switches to check if we need to debounce.
      if (current - event.m_time < DEBOUNCE_TIME) break;

      // We look at future event and see if we have an event in the queue that match this key with an opposite state withing the debouncing time frame.
      bool debounced = false;
      for (EventQueue::Iterator it : s_events) {
        // We are outside the debouncing time frame, we can stop looking for events.
        if (s_events[it].m_time - event.m_time > DEBOUNCE_TIME) {
          break;
        }

        // If the event is for the same key and with the opposite state, we have can debounce.
        if (s_events[it].m_pos == event.m_pos && s_events[it].m_isPressed != event.m_isPressed) {
          debugPrint("Cancel key\n");

          // We remove the current event and the opposite one.
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

      // We debounced the current event, go to the next event in the main event processing loop.
      if (debounced) continue;
    }

    // We process "on release" key presses.
    // "on release" key presses are key that trigger an output if they when they are relased if:
    //   - They were released within MAX_HOLD_TIME of being pressed
    //   - No other key was pressed in between.
    // Typically this is for keys that are associated with layer changes, but output a character when being tapped.
    if (onRelease[event.m_pos.m_line][event.m_pos.m_column] && event.m_isPressed) {
      // We have a key press for a "on release" key.

      EventQueue::Iterator nextIt = s_events.next(s_events.begin());
      if (nextIt != s_events.end()) {
        const Event& nextEvent = s_events.peek();
        if (nextEvent.m_pos == event.m_pos && !nextEvent.m_isPressed && nextEvent.m_time - event.m_time < MAX_HOLD_TIME) {
          // The next event is releasing the same key within the MAX_HOLD_TIME, we output a key press.
          debugPrintln("Press and release key");

          s_currentPressCount[event.m_pos.m_line][event.m_pos.m_column]++;
          sendCurrentKeyPress();

          delay(KEY_PRESS_LENGTH);  // milliseconds

          s_currentPressCount[event.m_pos.m_line][event.m_pos.m_column]--;
          sendCurrentKeyPress();

          // Remove the release event from the queue and go the the next event in the main event processing loop.
          s_events.popFront();
          s_events.popFront();
          continue;
        } else {
          //The next event is unrelated to the "on release" key behavior, we can continue processing the current event.
        }
      } else {
        // We need to see the next event to understand how to interpret this key event. Break the event processing loop to pull for events.
        break;
      }

      // TODO: Old behavior, remove.
      //// We try to find if we can find an event for the "on release" key (and its location in the queue) and if we have any other key press in between.
      //bool foundRelease = false;
      //EventQueue::Iterator releaseIndex;
      //bool foundAnotherPress = false;
      //for (EventQueue::Iterator it : s_events) {
      //  if (s_events[it].m_pos == event.m_pos && !s_events[it].m_isPressed) {
      //    foundRelease = true;
      //    releaseIndex = it;
      //    break;
      //  }
      //
      //  if (s_events[it].m_pos != event.m_pos && s_events[it].m_isPressed) {
      //    foundAnotherPress = true;
      //    break;
      //  }
      //}
      //
      //if (!foundAnotherPress) {
      //  if (foundRelease) {
      //    if (s_events[releaseIndex].m_time - event.m_time < MAX_HOLD_TIME) {
      //      debugPrintln("Press and release key");
      //
      //      s_currentPressCount[event.m_pos.m_line][event.m_pos.m_column]++;
      //      sendCurrentKeyPress();
      //
      //      delay(KEY_PRESS_LENGTH);  // milliseconds
      //
      //      s_currentPressCount[event.m_pos.m_line][event.m_pos.m_column]--;
      //      sendCurrentKeyPress();
      //    }
      //    s_events.popFront();
      //    s_events.remove(releaseIndex);
      //  }
      //
      //  break;
      //}
    }

#if DEBUG_LOG
    debugPrint("processing event ");
    debugPrint(s_events.begin());
    debugPrint(" ");
    event.print();
    debugPrintln();
#endif

    // Update the press count for that key.
    uint8_t& pressCount = s_currentPressCount[event.m_pos.m_line][event.m_pos.m_column];
    if (event.m_isPressed) {
      pressCount++;
    } else {
      if (pressCount > 0) pressCount--;
    }

    // If this key has a "forced key" associated wit it, we grab it.
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
          if (immuneToReset[line][column]) continue;  // Except key that are immune to reset.
          s_currentPressCount[line][column] = 0;
        }
      }

      // Reset the forced key.
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

    // Output the current state of key pressed to the USB host.
    debugPrintln("Send event:");
    sendCurrentKeyPress();


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
