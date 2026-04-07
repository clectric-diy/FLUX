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
#include <Adafruit_SSD1306.h>
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

#define ENCODER2_A     4
#define ENCODER2_B     5
#define ENCODER2_BTN   6

#define ENCODER1_A     7
#define ENCODER1_B     10
#define ENCODER1_BTN   11

// ============================================================================
// I2C DEVICE ADDRESSES
// ============================================================================
#define ADG2188_INPUT_MUX1_ADDR   0x48
#define ADG2188_INPUT_MUX2_ADDR   0x49
#define ADG2188_MAIN_ROUTER_ADDR  0x4A

#define OLED_ADDR   0x3C
#define OLED_WIDTH  128
#define OLED_HEIGHT 64

// ============================================================================
// CONSTANTS
// ============================================================================
#define MATRIX_SIZE         8
#define MATRIX_BYTES        8
#define NUM_PRESETS         8
#define AUDITION_TIMEOUT_MS 3000
#define EEPROM_SAVE_DELAY   1000

#define BOX_SIZE       6
#define BOX_SPACING    1
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
extern Adafruit_SSD1306 display;
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
