#pragma once

// Configuration dor logs, if any of the following setting is enabled the program will initialize the serial output.

// If enabled, the program will write in the debug output the state of the event buffer and how it was processed.
#define DEBUG_LOG 0
// If enabled, the program will write in the debug output a message each I2C device that had to be reset due to an error being detected.
#define I2C_RESET_LOG 0
// If enabled, the program will write in the debug output data about performance.
#define PERF_LOG 0

#define ANY_LOG DEBUG_LOG || I2C_RESET_LOG || PERF_LOG


// Version of the board, define what MCPs are available.
#define VERSION 2

// Number of line/colums in the virtual matrix of keys. they are not necessarly all mapped to real keys on the board.
#define NUM_LINES 5
#define NUM_COLUMNS 12

// Sequence of press-release or release-press under this time are ignored. Very short presses can be caused by the mecanical switch "bouncing", causing erroneous presses
#define DEBOUNCE_TIME 10000 // micro-seconds

// If enabled, a "tap or layer" key (such as space) that is released slightly too late after the next key was pressed is treated as a tap instead of activating its layer. This fixes typing " a" and getting " à". See overlap.h.
#define OVERLAP_REMOVAL 1

// Maximum delay between another key being pressed and the release of a "tap or layer" key for that overlap to be considered accidental rather than a deliberate use of the layer.
// This is also the worst case latency added when deliberately using such a layer, so it should stay small.
#define OVERLAP_REMOVAL_TIME 30000 // micro-seconds

// For "on release" keys (i.e., for key that are both used as layer and standard key), this is the maximum hold time for the key to be considered a standard press rather than a layer selection.
#define MAX_HOLD_TIME 500000 // micro-seconds

// When the code simulate a single instantaneous key press, this is how long the key is hold for the computer to read.
#define KEY_PRESS_LENGTH 50 // milli-seconds

#if ANY_LOG
#include <Arduino.h>
#define debugPrint(x) Serial.print(x)
#define debugPrintln(x) Serial.println(x)
#define ON_DEBUG(x) x
#else
#define debugPrint(x)
#define debugPrintln(x)
#define ON_DEBUG(x)
#endif