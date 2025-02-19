#include "mbed.h"
#include "config.h"
#include "MCP23008.hpp"
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

static const Pos s_mcpToPos[8][8] = {
#if VERSION == 1
  { Pos{ 0, 6 }, Pos{ 1, 6 }, Pos{ 2, 6 }, Pos{ 3, 6 }, Pos{ 4, 6 }, Pos{ 4, 7 }, Pos{ 3, 7 }, Pos{ 2, 7 } },
  { Pos{ 0, 7 }, Pos{ 1, 7 }, Pos{ 4, 8 }, Pos{ 3, 8 }, Pos{ 2, 8 }, Pos{ 1, 8 }, Pos{ 0, 8 }, Pos{ 4, 9 } },
  { Pos{ 3, 9 }, Pos{ 2, 9 }, Pos{ 1, 9 }, Pos{ 4, 10 }, Pos{ 3, 10 }, Pos{ 0, 9 }, Pos{ 2, 10 }, Pos{ 1, 10 } },
  { Pos{ 4, 11 }, Pos{ 3, 11 }, Pos{ 2, 11 }, Pos{ 0, 10 }, Pos{ 1, 11 }, Pos{ 0, 11 }, Pos{ -1, -1 }, Pos{ -1, -1 } },
  { Pos{ 0, 0 }, Pos{ 1, 0 }, Pos{ 2, 0 }, Pos{ 3, 0 }, Pos{ 4, 0 }, Pos{ 4, 1 }, Pos{ 3, 1 }, Pos{ 2, 1 } },
  { Pos{ 0, 1 }, Pos{ 1, 1 }, Pos{ 4, 2 }, Pos{ 3, 2 }, Pos{ 2, 2 }, Pos{ 1, 2 }, Pos{ 0, 2 }, Pos{ 4, 3 } },
  { Pos{ 3, 3 }, Pos{ 2, 3 }, Pos{ 1, 3 }, Pos{ 4, 4 }, Pos{ 3, 4 }, Pos{ 0, 3 }, Pos{ 2, 4 }, Pos{ 1, 4 } },
  { Pos{ 4, 5 }, Pos{ 3, 5 }, Pos{ 2, 5 }, Pos{ 0, 4 }, Pos{ 1, 5 }, Pos{ 0, 5 }, Pos{ -1, -1 }, Pos{ -1, -1 } },
#else
  { Pos{ 4, 8 }, Pos{ 4, 7 }, Pos{ 4, 6 }, Pos{ 3, 7 }, Pos{ 3, 6 }, Pos{ 2, 6 }, Pos{ -1, -1 }, Pos{ -1, -1 } },
  { Pos{ 2, 7 }, Pos{ 3, 8 }, Pos{ 2, 8 }, Pos{ 1, 8 }, Pos{ 1, 7 }, Pos{ 1, 6 }, Pos{ -1, -1 }, Pos{ -1, -1 } },
  { Pos{ 3, 9 }, Pos{ 3, 10 }, Pos{ 2, 10 }, Pos{ 1, 10 }, Pos{ 1, 9 }, Pos{ 2, 9 }, Pos{ -1, -1 }, Pos{ -1, -1 } },

  { Pos{ 4, 3 }, Pos{ 4, 4 }, Pos{ 4, 5 }, Pos{ 3, 4 }, Pos{ 3, 5 }, Pos{ 2, 5 }, Pos{ -1, -1 }, Pos{ -1, -1 } },
  { Pos{ 2, 4 }, Pos{ 3, 3 }, Pos{ 2, 3 }, Pos{ 1, 3 }, Pos{ 1, 4 }, Pos{ 1, 5 }, Pos{ -1, -1 }, Pos{ -1, -1 } },
  { Pos{ 3, 2 }, Pos{ 3, 1 }, Pos{ 2, 1 }, Pos{ 1, 1 }, Pos{ 1, 2 }, Pos{ 2, 2 }, Pos{ -1, -1 }, Pos{ -1, -1 } },
#endif
};

bool s_switches[NUM_LINES][NUM_COLUMNS];
uint8_t s_currentPressCount[NUM_LINES][NUM_COLUMNS];

struct Mcp {
  MCP23008* m_mcp;
  uint8_t m_offset;
};

//I2C i2c(p8, p9);
I2C* s_i2c = nullptr;
MCP23008 mcp0(0);
MCP23008 mcp1(1);
MCP23008 mcp2(2);
#if VERSION == 1
MCP23008 mcp3(3);
#endif
MCP23008 mcp4(4);
MCP23008 mcp5(5);
MCP23008 mcp6(6);
#if VERSION == 1
MCP23008 mcp7(7);
#endif

static const Mcp s_mcps[] = {
#if VERSION == 1
  Mcp{ &mcp0, 0 },
  Mcp{ &mcp1, 1 },
  Mcp{ &mcp2, 2 },
  Mcp{ &mcp3, 3 },
  Mcp{ &mcp4, 4 },
  Mcp{ &mcp5, 5 },
  Mcp{ &mcp6, 6 },
  Mcp{ &mcp7, 7 },
#else
  Mcp{ &mcp0, 0 },
  Mcp{ &mcp1, 1 },
  Mcp{ &mcp2, 2 },
  Mcp{ &mcp4, 3 },
  Mcp{ &mcp5, 4 },
  Mcp{ &mcp6, 5 },
#endif
};

Keyboard keyboard;

EventQueue s_events;
LayerTracker s_layerTracker;

class KeyboardOutput {
public:
  void add(Key k) {
    switch (k) {
      case Key::CTRL: m_modifiers = m_modifiers | MOD_CTRL; break;
      case Key::SHIFT: m_modifiers = m_modifiers | MOD_SHIFT; break;
      case Key::ALT: m_modifiers = m_modifiers | MOD_ALT; break;
      case Key::WIN: m_modifiers = m_modifiers | MOD_WIN; break;
      case Key::RCTRL: m_modifiers = m_modifiers | MOD_RCTRL; break;
      case Key::RSHIFT: m_modifiers = m_modifiers | MOD_RSHIFT; break;
      case Key::RALT: m_modifiers = m_modifiers | MOD_RALT; break;
      case Key::RWIN: m_modifiers = m_modifiers | MOD_RWIN; break;

      default:
        if (m_nextKey < 6) {
          m_keys[m_nextKey] = uint8_t(k);
          m_nextKey++;
        }
        break;
    }
  }

  void add(MediaKey k) {
    m_mediaKey = uint8_t(k);
  }

  inline void add(const K& keys) {
    if (keys.m_key0 != Key::NONE) add(keys.m_key0);
    if (keys.m_key1 != Key::NONE) add(keys.m_key1);
    if (keys.m_mediaKey != MediaKey::NONE) add(keys.m_mediaKey);
  }

  void send() {
    keyboard.press(m_keys, m_modifiers, m_mediaKey);
  }

  inline bool isAnyKeyPressed() {
    return m_modifiers != 0 || m_nextKey != 0;
  }
#if DEBUG_LOG
  void print() const;
#endif

private:
  uint8_t m_modifiers = 0;
  uint8_t m_keys[6] = { 0, 0, 0, 0, 0, 0 };
  int m_nextKey = 0;
  uint8_t m_mediaKey = 0;
};

#if DEBUG_LOG
void KeyboardOutput::print() const {
  debugPrint("KeyboardOutput(modifiers: ");
  if (m_modifiers & MOD_CTRL) debugPrint("Ctrl ");
  if (m_modifiers & MOD_SHIFT) debugPrint("Shift ");
  if (m_modifiers & MOD_ALT) debugPrint("Alt ");
  if (m_modifiers & MOD_WIN) debugPrint("Win ");
  if (m_modifiers & MOD_RCTRL) debugPrint("Left_Ctrl ");
  if (m_modifiers & MOD_RSHIFT) debugPrint("Left_Shift ");
  if (m_modifiers & MOD_RALT) debugPrint("Left_Alt ");
  if (m_modifiers & MOD_RWIN) debugPrint("Left_Win ");
  debugPrint("keys: ");
  debugPrint(m_keys[0]);
  debugPrint(" ");
  debugPrint(m_keys[1]);
  debugPrint(" ");
  debugPrint(m_keys[2]);
  debugPrint(" ");
  debugPrint(m_keys[3]);
  debugPrint(" ");
  debugPrint(m_keys[4]);
  debugPrint(" ");
  debugPrint(m_keys[5]);
  debugPrint(" media_key: ");
  debugPrint(m_mediaKey);
  debugPrint(")");
}
#endif

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
        output.add(key);
      }
    }
  }

  if (!output.isAnyKeyPressed() && s_layerTracker.mask() == 1) {
    output.add(Key::SHIFT);
    debugPrintln("\tAdding shift");
  }

  if (s_forcedKey != Key::NONE) {
    output.add(s_forcedKey);
  }
}

void resetI2C() {
  if (s_i2c != nullptr) {
    delete s_i2c;
  }
  s_i2c = new I2C(p8, p9);
  s_i2c->frequency(400000);
  //s_i2c->frequency(100000);

  for (const Mcp& mcp : s_mcps) {
    mcp.m_mcp->setI2C(s_i2c);
    mcp.m_mcp->reset();
    mcp.m_mcp->set_input_pins(MCP23008::Pin_All);
    mcp.m_mcp->set_pullups(MCP23008::Pin_All);
  }
}

void setup() {
#if ANY_LOG
  Serial.begin(9600);
#endif

  resetI2C();

  for (int line = 0; line < NUM_LINES; ++line) {
    for (int column = 0; column < NUM_COLUMNS; ++column) {
      s_switches[line][column] = false;
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

  ON_DEBUG(bool anyNewEvent = false);
  bool mcpError = false;
#if I2C_RESET_LOG
  int mcpErrorOffset;
#endif

  for (const Mcp& mcp : s_mcps) {
    uint8_t pins = mcp.m_mcp->read_inputs();
    if (mcp.m_mcp->isError()) {
      mcpError = true;
#if I2C_RESET_LOG
      mcpErrorOffset = mcp.m_offset;
#endif
      break;
    }

    for (int pin = 0; pin < 8; ++pin) {
      Pos pos = s_mcpToPos[mcp.m_offset][pin];
      if (pos.m_line >= 0) {
        bool isPressed = !(pins & (1 << pin));
        if (s_switches[pos.m_line][pos.m_column] != isPressed) {
          s_switches[pos.m_line][pos.m_column] = isPressed;

          Event& event = s_events.emplaceBack();
          event.m_pos = pos;
          event.m_isPressed = isPressed;
          event.m_time = micros();

          ON_DEBUG(anyNewEvent = true);
        }
      }
    }
  }

#if DEBUG_LOG
  if (anyNewEvent) {
    debugPrintln("new events added to queue:");
    s_events.print("\t");
  }
#endif

  if (mcpError) {
    for (int i = 0; i < 10; ++i) {
      delete s_i2c;
      s_i2c = new I2C(p8, p9);
    }
    resetI2C();

#if I2C_RESET_LOG
    Serial.print("Reset i2c (source offset: ");
    Serial.print(mcpErrorOffset);
    Serial.println(")");
#endif
    return;
  }

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
      for(int line = 0; line < NUM_LINES; ++line) {
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
