#pragma once
#include "config.h"

/// Relate to everything about reading switches
namespace Input {

// Initialize the input system. Must be called once before any other functions.
void init();

// Read the state of the key switched.
void step();

// Return true if the key at coordinated (line, column) is pressed.
bool isPressed(int line, int column);

// Return true if the status of the key at coordinate (line, column) has changed with the last to step().
bool hasChanged(int line, int column);
}