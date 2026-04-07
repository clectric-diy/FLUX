/*
  ============================================================================
  Nexus Core - Shared hardware abstraction and UI framework
  ============================================================================
  This header contains all shared definitions, classes, and function declarations
  for the Nexus firmware family (router, lunetta, sequencer).
  
  Each .ino variant includes this file and implements its own setup()/loop().
  ============================================================================
*/

#ifndef NEXUS_CORE_H
#define NEXUS_CORE_H

#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// ============================================================================
// SERIAL DEBUG INFRASTRUCTURE
// ============================================================================
#define SERIAL_DEBUG_ENABLED 1  // Set to 0 to disable all serial output
#define SERIAL_BAUD 115200

#if SERIAL_DEBUG_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt, ...)
#endif

// ============================================================================
// PIN DEFINITIONS
// ============================================================================
// Prototype target board: Arduino Nano Every (ATmega4809, 5V logic)
// I2C uses the board's dedicated SDA/SCL pins.

#if !defined(ARDUINO_AVR_NANO_EVERY)
  #warning "Nexus prototype firmware is currently mapped for Arduino Nano Every"
#endif

#define ENCODER1_A     4    // Encoder 1 A pin (interrupt)
#define ENCODER1_B     5    // Encoder 1 B pin (interrupt)
#define ENCODER1_BTN   6    // Encoder 1 push button

#define ENCODER2_A     7    // Encoder 2 A pin (interrupt)
#define ENCODER2_B     10    // Encoder 2 B pin (interrupt)
#define ENCODER2_BTN   11   // Encoder 2 push button

// ============================================================================
// I2C DEVICE ADDRESSES
// ============================================================================
#define ADG2188_INPUT_MUX1_ADDR   0x48  // First input mux
#define ADG2188_INPUT_MUX2_ADDR   0x49  // Second input mux
#define ADG2188_MAIN_ROUTER_ADDR  0x4A  // Main routing matrix

#define OLED_ADDR   0x3C  // SSD1309 I2C module address (SSD1306-compatible init path)
#define OLED_WIDTH  128
#define OLED_HEIGHT 64

// ============================================================================
// CONSTANTS
// ============================================================================
#define MATRIX_SIZE         8           // 8x8 switch matrix
#define MATRIX_BYTES        8           // 8 bytes = 64 switches
#define NUM_PRESETS         8           // 8 preset slots
#define AUDITION_TIMEOUT_MS 3000        // 3-second audition timeout
#define EEPROM_SAVE_DELAY   1000        // Opportunistic save delay

// Display grid (centered on 128x64 landscape layout)
#define BOX_SIZE       6
#define BOX_SPACING    1
#define GRID_PIXEL_SIZE ((MATRIX_SIZE * BOX_SIZE) + ((MATRIX_SIZE - 1) * BOX_SPACING))
#define GRID_START_X   ((OLED_WIDTH - GRID_PIXEL_SIZE) / 2)
#define GRID_START_Y   ((OLED_HEIGHT - GRID_PIXEL_SIZE) / 2)

// ============================================================================
// ENUM: UI STATE MACHINE
// ============================================================================
enum UIMode {
  ROUTING_MODE,  // Normal matrix patching (encoders control X/Y, audition on turn)
  MENU_MODE      // Settings/preset selection (menu navigation)
};

// ============================================================================
// CLASS: PresetState
// ============================================================================
// Encapsulates a complete routing preset. Can be extended by subclasses
// (nexus-lunetta, nexus-sequencer) to include additional parameters.
class PresetState {
public:
  // Core data
  byte patchMatrix[MATRIX_BYTES];  // 8x8 switch state (1 byte per output row)
  
  // Metadata
  unsigned long lastModified;      // Timestamp of last change
  
  // Constructor
  PresetState() {
    memset(patchMatrix, 0, MATRIX_BYTES);
    lastModified = 0;
  }
  
  // Clear preset to all switches open
  void clear() {
    memset(patchMatrix, 0, MATRIX_BYTES);
    lastModified = millis();
  }
  
  // Get a specific matrix entry (row, col)
  // Returns 1 if switch is ON, 0 if OFF
  bool getEntry(byte row, byte col) {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) return false;
    return bitRead(patchMatrix[row], col);
  }
  
  // Set a specific matrix entry (row, col)
  void setEntry(byte row, byte col, bool state) {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) return;
    bitWrite(patchMatrix[row], col, state);
    lastModified = millis();
  }
  
  // Toggle a specific matrix entry
  void toggleEntry(byte row, byte col) {
    if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) return;
    bitWrite(patchMatrix[row], col, !bitRead(patchMatrix[row], col));
    lastModified = millis();
  }
  
  // Compare two states and return XOR (for audition calculations)
  void getAuditDiff(const PresetState& other, byte* diffMatrix) {
    for (int i = 0; i < MATRIX_BYTES; i++) {
      diffMatrix[i] = patchMatrix[i] ^ other.patchMatrix[i];
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

// Preset management
PresetState& presetAt(byte idx);
void loadActiveFromPreset(byte idx);
void saveActiveToPreset(byte idx);
void nextPreset();
void prevPreset();

// I2C & Hardware
void initializeADG2188();
void writePatchMatrixToADG2188();
void writeAuditToADG2188(byte col, byte row);
void writeToADG2188(byte address, byte data);
void writeToADG2188Row(byte address, byte row, byte data);

// EEPROM
void savePresetToEEPROM(byte presetIndex);
void loadPresetFromEEPROM(byte presetIndex);

// Display
void updateDisplay();
void renderRoutingMode();
void renderMenuMode();
void renderMatrixBox(byte x, byte y);

// Utilities
void printDebugStatus();

#endif // NEXUS_CORE_H
