/*
  ============================================================================
  File: nexus-core.h
  ============================================================================
  Purpose
  -------
  This header defines the shared interface for Nexus firmware.
  It gives all sketches one common vocabulary for runtime behavior, constants,
  hardware pins, and function declarations.

  What lives here
  ---------------
  1) Hardware and layout constants
  2) Shared data structures (for example, `Patch`)
  3) Global runtime variable declarations used across runtime code
  4) Public function declarations for setup, loop, display, I2C, and memory

  How to use it
  -------------
  - Include this file from Nexus sketches that use shared runtime behavior.
  - Keep declarations here and put implementation details in nexus-core.cpp.
  - Most sketches should call `nexus_setup()` and `nexus_loop()`.
  ============================================================================
*/

#ifndef NEXUS_CORE_LIBRARY_H
#define NEXUS_CORE_LIBRARY_H

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include <stdarg.h>

// ============================================================================
// SERIAL DEBUG INFRASTRUCTURE
// ============================================================================
// Set one variable to choose debug output detail:
// 0 = OFF, 1 = ERROR, 2 = INFO, 3 = VERBOSE
#ifndef SERIAL_DEBUG_LEVEL
#define SERIAL_DEBUG_LEVEL 2
#endif

#define SERIAL_DEBUG_ENABLED (SERIAL_DEBUG_LEVEL > 0)
#define SERIAL_BAUD 115200

void nexusDebugPrintf(const char* format, ...);

#if SERIAL_DEBUG_LEVEL >= 2
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) nexusDebugPrintf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

#if SERIAL_DEBUG_LEVEL >= 3
  #define DEBUG_VERBOSE_PRINTF(...) nexusDebugPrintf(__VA_ARGS__)
#else
  #define DEBUG_VERBOSE_PRINTF(...)
#endif

// ============================================================================
// PIN DEFINITIONS for ATMega4809 (and Arduino Nano Every)
// ============================================================================

#define ENCODER2_A     14  // D14 (Nano Every pin 4)
#define ENCODER2_B     15  // D15 (Nano Every pin 5)
#define ENCODER2_BTN   16  // D16 (Nano Every pin 6)

#define ENCODER1_A     17  // D17 (Nano Every pin 7)
#define ENCODER1_B     20  // D20 (Nano Every pin 10)
#define ENCODER1_BTN   21  // D21 (Nano Every pin 11)

// ============================================================================
// ROUTING SWITCH SETTINGS (ADG2188)
// ============================================================================

// Three ADG2188 switch chips handle signal routing.
// - INPUT_SWITCH: routes signals arriving from external inputs
#define INPUT_SWITCH_I2C_ADDR   0x48

// - GENERATED_SWITCH: routes signals generated inside the module by the ATMega
#define GENERATED_SWITCH_I2C_ADDR   0x49

// - ROUTING_SWITCH: the main 8x8 routing matrix
#define ROUTING_SWITCH_I2C_ADDR      0x4A

// ============================================================================
// ROUTING MATRIX SETTINGS
// ============================================================================

// The routing matrix is 8x8 and is stored as 8 bytes.
#define MATRIX_SIZE         8
#define MATRIX_BYTES        8

// We store 8 preset patches in addition to the active routing matrix.
#define NUM_PRESETS         8

// Memory storage layout:

// - Active patch is stored first.
#define MEMORY_ACTIVE_PATCH_ADDR    0

// - Each preset stores MATRIX_BYTES.
#define MEMORY_PRESET_STRIDE        MATRIX_BYTES

// - Presets start immediately after the active patch block.
#define MEMORY_PRESET_BASE_ADDR     (MEMORY_ACTIVE_PATCH_ADDR + MATRIX_BYTES)

// - An initialization marker tells us whether memory has valid saved data at initial power-up.
#define MEMORY_INIT_MARKER_ADDR     (MEMORY_PRESET_BASE_ADDR + (NUM_PRESETS * MEMORY_PRESET_STRIDE))
#define MEMORY_INIT_MARKER_VAL      0xAB

// ============================================================================
// OLED DISPLAY SETTINGS
// ============================================================================

// The Nexus Module uses a 1.3" 128x64 OLED display with a SH1106 driver.

#define OLED_I2C_ADDR       0x3C
#define OLED_WIDTH          128
#define OLED_HEIGHT         64

#define OLED_PIXEL_X_OFFSET       2
#define OLED_PIXEL_Y_OFFSET       0

// Grid drawing sizes for the routing view on the OLED.
#define BOX_SIZE       6
#define BOX_SPACING    1

#define GRID_PIXEL_SIZE ((MATRIX_SIZE * BOX_SIZE) + ((MATRIX_SIZE - 1) * BOX_SPACING))

#define GRID_START_X   ((OLED_WIDTH - GRID_PIXEL_SIZE) / 2)
#define GRID_START_Y   ((OLED_HEIGHT - GRID_PIXEL_SIZE) / 2)

// ============================================================================
// TIMING CONSTANTS
// ============================================================================

// Timing settings (in milliseconds):
// - How long a temporary audition stays active.
#define AUDITION_TIMEOUT_MS 3000

// - How long we wait before writing updates to memory.
#define MEMORY_SAVE_DELAY   1000

// ============================================================================
// ENUM: UI STATE MACHINE
// ============================================================================

// The state machine switches between routing and menu modes when the 
// first encoder is clicked.

enum UIMode {
  ROUTING_MODE,
  MENU_MODE
};

// ============================================================================
// CLASS: Patch
// ============================================================================
  
// Patch stores one complete patch from the routing matrix.
// Each row is one byte, and each bit in that byte is one connection on/off value.

class Patch {
public:
  byte patchConnections[MATRIX_BYTES];

  Patch() {
    memset(patchConnections, 0, MATRIX_BYTES);
  }

  void clear() {
    memset(patchConnections, 0, MATRIX_BYTES);
  }

  bool isPatchConnectionActive(byte row, byte col) const {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) {
      return false;
    }
    return bitRead(patchConnections[row], col);
  }

  void setPatchConnection(byte row, byte col, bool isConnected) {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) {
      return;
    }
    bitWrite(patchConnections[row], col, isConnected);
  }

  void togglePatchConnection(byte row, byte col) {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) {
      return;
    }
    bitWrite(patchConnections[row], col, !bitRead(patchConnections[row], col));
  }
};

// ============================================================================
// GLOBAL VARIABLES - DECLARED HERE, DEFINED IN nexus-core.cpp
// ============================================================================
extern U8G2_SH1106_128X64_NONAME_F_HW_I2C display;
extern Patch activePatch;
extern byte currentPresetIndex;

extern UIMode uiMode;
extern byte cursorX;
extern byte cursorY;
extern byte auditingX;
extern byte auditingY;
extern bool isAuditioning;
extern unsigned long auditTimeoutMs;

extern volatile int8_t encoder1Delta;
extern volatile int8_t encoder2Delta;
extern volatile bool flagSaveNeeded;
extern volatile bool flagDisplayUpdate;
extern volatile bool flagModeChange;

extern unsigned long lastMemorySaveTime;

// ============================================================================
// CORE FUNCTION DECLARATIONS
// ============================================================================
void nextPreset();
void prevPreset();

void initializeRoutingSwitches();
void writeActivePatchToRoutingSwitches();
void writeAuditToRoutingSwitch(byte col, byte row);
void writeToSwitch(byte address, byte data);
void writeSwitchRegister(byte address, byte registerIndex, byte data);

void loadPresetFromMemory(byte presetIndex);
void saveActivePatchToMemory();
void loadActivePatchFromMemory();

void updateDisplay();
void renderRoutingMode();
void renderMenuMode();
void renderMatrixBox(byte x, byte y);

// ============================================================================
// ROUTER RUNTIME ENTRY POINTS
// ============================================================================
void nexus_setup();
void nexus_loop();

#endif // NEXUS_CORE_LIBRARY_H
