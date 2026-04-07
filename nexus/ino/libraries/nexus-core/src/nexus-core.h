/*
  ============================================================================
  nexus-core - Shared hardware abstraction and router runtime for Nexus firmware
  ============================================================================
  This library contains the shared definitions, classes, and function
  declarations used by the Nexus firmware family (Router, Lunetta, Sequencer).

  Router uses the library's `nexus_setup()` / `nexus_loop()` entry points.
  Other variants may call the lower-level shared functions directly and keep
  their own variant-specific setup/loop behavior.
  ============================================================================
*/

#ifndef NEXUS_CORE_LIBRARY_H
#define NEXUS_CORE_LIBRARY_H

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include <stdarg.h>

#ifndef OLED_RESET
#define OLED_RESET -1
#endif

// ============================================================================
// SERIAL DEBUG INFRASTRUCTURE
// ============================================================================
#define SERIAL_DEBUG_ENABLED 1
#define SERIAL_BAUD 115200

void nexusDebugPrintf(const char* format, ...);

#if SERIAL_DEBUG_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) nexusDebugPrintf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

// ============================================================================
// PIN DEFINITIONS
// ============================================================================
#if !defined(ARDUINO_AVR_NANO_EVERY)
  #warning "Nexus prototype firmware is currently mapped for Arduino Nano Every"
#endif

#define ENCODER2_A     14   // Physical pin 4  = A0/D14
#define ENCODER2_B     15   // Physical pin 5  = A1/D15
#define ENCODER2_BTN   16   // Physical pin 6  = A2/D16

#define ENCODER1_A     17   // Physical pin 7  = A3/D17
#define ENCODER1_B     20   // Physical pin 10 = A6/D20 (skips SDA/SCL at 8,9)
#define ENCODER1_BTN   21   // Physical pin 11 = A7/D21

// ============================================================================
// I2C DEVICE ADDRESSES
// ============================================================================
#define ADG2188_INPUT_MUX1_ADDR   0x48
#define ADG2188_INPUT_MUX2_ADDR   0x49
#define ADG2188_MAIN_ROUTER_ADDR  0x4A

#define OLED_ADDR   0x3C
#define OLED_WIDTH  128
#define OLED_HEIGHT 64

// 1.3" SH1106-class 128x64 modules commonly need a +2 X pixel shift when driven
// through SSD1306-compatible libraries.
// Tune these if content appears misaligned.
#define OLED_PIXEL_OFFSET_X       2
#define OLED_PIXEL_OFFSET_Y       0
#define OLED_CONTROLLER_OFFSET_Y  0

// ============================================================================
// CONSTANTS
// ============================================================================
#define MATRIX_SIZE         8
#define MATRIX_BYTES        8
#define NUM_PRESETS         8
#define AUDITION_TIMEOUT_MS 3000
#define EEPROM_SAVE_DELAY   1000

// Magic byte stored after the preset region to detect first boot vs uninitialised EEPROM
#define EEPROM_PRESET_STRIDE  (MATRIX_BYTES + 4)          // 12 bytes per preset slot
#define EEPROM_MAGIC_ADDR     (NUM_PRESETS * EEPROM_PRESET_STRIDE)  // byte 96
#define EEPROM_MAGIC_VAL      0xAB

#define BOX_SIZE       6
#define BOX_SPACING    2
#define GRID_PIXEL_SIZE ((MATRIX_SIZE * BOX_SIZE) + ((MATRIX_SIZE - 1) * BOX_SPACING))
#define GRID_START_X   ((OLED_WIDTH - GRID_PIXEL_SIZE) / 2)
#define GRID_START_Y   ((OLED_HEIGHT - GRID_PIXEL_SIZE) / 2)

// ============================================================================
// ENUM: UI STATE MACHINE
// ============================================================================
enum UIMode {
  ROUTING_MODE,
  MENU_MODE
};

// ============================================================================
// CLASS: PresetState
// ============================================================================
class PresetState {
public:
  byte patchMatrix[MATRIX_BYTES];
  unsigned long lastModified;

  PresetState() {
    memset(patchMatrix, 0, MATRIX_BYTES);
    lastModified = 0;
  }

  void clear() {
    memset(patchMatrix, 0, MATRIX_BYTES);
    lastModified = millis();
  }

  bool getEntry(byte row, byte col) {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) {
      return false;
    }
    return bitRead(patchMatrix[row], col);
  }

  void setEntry(byte row, byte col, bool state) {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) {
      return;
    }
    bitWrite(patchMatrix[row], col, state);
    lastModified = millis();
  }

  void toggleEntry(byte row, byte col) {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) {
      return;
    }
    bitWrite(patchMatrix[row], col, !bitRead(patchMatrix[row], col));
    lastModified = millis();
  }

  void getAuditDiff(const PresetState& other, byte* diffMatrix) {
    for (int index = 0; index < MATRIX_BYTES; index++) {
      diffMatrix[index] = patchMatrix[index] ^ other.patchMatrix[index];
    }
  }
};

// ============================================================================
// GLOBAL VARIABLES - DECLARED HERE, DEFINED IN nexus-core.cpp
// ============================================================================
extern U8G2_SH1106_128X64_NONAME_F_HW_I2C display;
extern PresetState activeState;
extern PresetState presets[NUM_PRESETS];
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

extern unsigned long lastEEPROMSaveTime;

// ============================================================================
// CORE FUNCTION DECLARATIONS
// ============================================================================
PresetState& presetAt(byte idx);
void loadActiveFromPreset(byte idx);
void saveActiveToPreset(byte idx);
void nextPreset();
void prevPreset();

void initializeADG2188();
void writePatchMatrixToADG2188();
void writeAuditToADG2188(byte col, byte row);
void writeToADG2188(byte address, byte data);
void writeToADG2188Row(byte address, byte row, byte data);

void savePresetToEEPROM(byte presetIndex);
void loadPresetFromEEPROM(byte presetIndex);

void updateDisplay();
void renderRoutingMode();
void renderMenuMode();
void renderMatrixBox(byte x, byte y);

void printDebugStatus();

// ============================================================================
// ROUTER RUNTIME ENTRY POINTS
// ============================================================================
void nexus_setup();
void nexus_loop();

#endif // NEXUS_CORE_LIBRARY_H
