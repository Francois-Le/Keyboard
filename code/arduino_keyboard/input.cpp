#include "input.h"
#include "pos.h"
#include "MCP23008.hpp"

#if VERSION == 1
#define NUM_MCP_CHIPS 8
#else
#define NUM_MCP_CHIPS 6
#endif

/// Namespace containing all the implementation details of the input system.
namespace InputImpl {

/// Indicate which key coordinates each input of each MCP is responsible for. m_mcp[i][j] is the key location for the j-th channel of the i-th MCP chip.
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

/// The I2C handle, setup by Input::Init().
I2C* s_i2c = nullptr;

/// All MCP IO handles, passing the addressed on the I2C bus.
static MCP23008 s_mcps[] = {
#if VERSION == 1
  MCP23008(0),
  MCP23008(1),
  MCP23008(2),
  MCP23008(3),
  MCP23008(4),
  MCP23008(5),
  MCP23008(6),
  MCP23008(7),
#else
  MCP23008(0),
  MCP23008(1),
  MCP23008(2),
  MCP23008(4),
  MCP23008(5),
  MCP23008(6),
#endif
};

/// The current state of every switch. True mean pressed, false mean released.
bool s_state[NUM_LINES][NUM_COLUMNS];

/// Indicate if the state each switch have changed in the last call to Input::step().
bool s_stateChanged[NUM_LINES][NUM_COLUMNS];

/// Reset the I2C bus and initialize each MCP chip.
void resetI2C() {
  if (s_i2c != nullptr) {
    delete s_i2c;
  }
  s_i2c = new I2C(p8, p9);
  s_i2c->frequency(400000);
  //s_i2c->frequency(100000);

  for (MCP23008& mcp : s_mcps) {
    mcp.setI2C(s_i2c);
    mcp.reset();
    mcp.set_input_pins(MCP23008::Pin_All);
    mcp.set_pullups(MCP23008::Pin_All);
  }
}
}

void Input::init() {
  using namespace InputImpl;

  resetI2C();

  // Set the initial state.
  for (int line = 0; line < NUM_LINES; ++line) {
    for (int column = 0; column < NUM_COLUMNS; ++column) {
      s_state[line][column] = false;
      s_stateChanged[line][column] = false;
      //s_currentPressCount[line][column] = 0;
    }
  }
}
void Input::step() {
  using namespace InputImpl;

  bool mcpError = false;
#if I2C_RESET_LOG
  int mcpErrorIndex;
#endif

  // We read all all the MCP first.
  // We do this first because we want to sure we can updates all the s_stateChanged otherwise if we stop midway through in case of error we would could skip some s_stateChanged.
  uint8_t pinsForMcp[NUM_MCP_CHIPS];
  for (int mcpIndex = 0; mcpIndex < NUM_MCP_CHIPS; ++mcpIndex) {
    pinsForMcp[mcpIndex] = s_mcps[mcpIndex].read_inputs();

    if (s_mcps[mcpIndex].isError()) {
      mcpError = true;
#if I2C_RESET_LOG
      mcpErrorIndex = mcpIndex;
#endif
      break;
    }
  }

  if (!mcpError) {
    // If there was no issue reading the MCPs, we update all the switches state.
    for (int mcpIndex = 0; mcpIndex < NUM_MCP_CHIPS; ++mcpIndex) {
      uint8_t pins = pinsForMcp[mcpIndex];

      for (int pin = 0; pin < 8; ++pin) {
        Pos pos = s_mcpToPos[mcpIndex][pin];
        if (pos.m_line >= 0) {
          bool isPressed = !(pins & (1 << pin));
          s_stateChanged[pos.m_line][pos.m_column] = s_state[pos.m_line][pos.m_column] != isPressed;
          s_state[pos.m_line][pos.m_column] = isPressed;
        }
      }
    }
  } else {
    // If we had an error, we reset the I2C bus.

    // First reset the bus a few time, this somehow help.
    for (int i = 0; i < 10; ++i) {
      delete s_i2c;
      s_i2c = new I2C(p8, p9);
    }

    // Actual reset.
    resetI2C();

#if I2C_RESET_LOG
    Serial.print("Reset i2c (chip index ");
    Serial.print(mcpErrorIndex);
    Serial.println(" was in error state)");
#endif
    //return;
  }
}

bool Input::isPressed(int line, int column) {
  using namespace InputImpl;

  return s_state[line][column];
}

bool Input::hasChanged(int line, int column) {
  using namespace InputImpl;

  return s_stateChanged[line][column];
}