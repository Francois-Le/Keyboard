#include "input.h"
#include "pos.h"
#include "MCP23008.hpp"

namespace InputImpl {
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

struct Mcp {
  MCP23008* m_mcp;
  uint8_t m_offset;
};

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

bool s_switches[NUM_LINES][NUM_COLUMNS];
bool s_switchesChanged[NUM_LINES][NUM_COLUMNS];

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
}

void Input::init() {
  using namespace InputImpl;

  resetI2C();

  for (int line = 0; line < NUM_LINES; ++line) {
    for (int column = 0; column < NUM_COLUMNS; ++column) {
      s_switches[line][column] = false;
      s_switchesChanged[line][column] = false;
      //s_currentPressCount[line][column] = 0;
    }
  }
}
void Input::step() {
  using namespace InputImpl;

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
        s_switchesChanged[pos.m_line][pos.m_column] = s_switches[pos.m_line][pos.m_column] != isPressed;
        s_switches[pos.m_line][pos.m_column] = isPressed;
      }
    }
  }

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
    //return;
  }
}

bool Input::isPressed(int line, int column) {
  using namespace InputImpl;

  return s_switches[line][column];
}

bool Input::hasChanged(int line, int column) {
  using namespace InputImpl;

  return s_switchesChanged[line][column];
}