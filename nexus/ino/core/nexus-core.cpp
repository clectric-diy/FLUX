/*
  ============================================================================
  NEXUS CORE - Implementation of shared functions
  ============================================================================
  Contains all I2C, EEPROM, display rendering, and preset management functions
  used across router, lunetta, and sequencer variants.
*/

#include "nexus-core.h"

// ============================================================================
// GLOBAL VARIABLE DEFINITIONS
// ============================================================================
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

PresetState activeState;
PresetState presets[NUM_PRESETS];
byte currentPresetIndex = 0;

UIMode uiMode = ROUTING_MODE;
byte cursorX = 0;
byte cursorY = 0;
byte auditingX = 0;
byte auditingY = 0;
bool isAuditioning = false;
unsigned long auditTimeoutMs = 0;

volatile int8_t encoder1Delta = 0;
volatile int8_t encoder2Delta = 0;
volatile bool flagSaveNeeded = false;
volatile bool flagDisplayUpdate = false;
volatile bool flagModeChange = false;

unsigned long lastEEPROMSaveTime = 0;

// ============================================================================
// PRESET MANAGEMENT
// ============================================================================

PresetState& presetAt(byte idx) {
  if (idx >= NUM_PRESETS) {
    idx = 0;
  }
  return presets[idx];
}

void loadActiveFromPreset(byte idx) {
  activeState = presetAt(idx);
  currentPresetIndex = idx;
  DEBUG_PRINTF("[PRESET] Loaded preset %d\n", idx);
}

void saveActiveToPreset(byte idx) {
  presetAt(idx) = activeState;
  currentPresetIndex = idx;
  DEBUG_PRINTF("[PRESET] Saved active to preset %d\n", idx);
}

void nextPreset() {
  currentPresetIndex = (currentPresetIndex + 1) % NUM_PRESETS;
  loadActiveFromPreset(currentPresetIndex);
  saveActiveToPreset(currentPresetIndex);
  savePresetToEEPROM(currentPresetIndex);
  DEBUG_PRINTF("[PRESET] Next -> %d\n", currentPresetIndex);
  flagDisplayUpdate = true;
}

void prevPreset() {
  if (currentPresetIndex == 0) {
    currentPresetIndex = NUM_PRESETS - 1;
  } else {
    currentPresetIndex--;
  }
  loadActiveFromPreset(currentPresetIndex);
  saveActiveToPreset(currentPresetIndex);
  savePresetToEEPROM(currentPresetIndex);
  DEBUG_PRINTF("[PRESET] Prev -> %d\n", currentPresetIndex);
  flagDisplayUpdate = true;
}

// ============================================================================
// I2C HARDWARE COMMUNICATION
// ============================================================================

void initializeADG2188() {
  DEBUG_PRINTLN(F("[I2C] Initializing ADG2188 switches..."));
  
  // Input mux 1
  writeToADG2188(ADG2188_INPUT_MUX1_ADDR, 0x00);
  
  // Input mux 2
  writeToADG2188(ADG2188_INPUT_MUX2_ADDR, 0x00);
  
  // Main router
  for (int row = 0; row < MATRIX_BYTES; row++) {
    writeToADG2188Row(ADG2188_MAIN_ROUTER_ADDR, row, 0x00);
  }
  
  DEBUG_PRINTLN(F("[I2C] ADG2188 initialization complete"));
}

void writePatchMatrixToADG2188() {
  for (int row = 0; row < MATRIX_BYTES; row++) {
    writeToADG2188Row(ADG2188_MAIN_ROUTER_ADDR, row, activeState.patchMatrix[row]);
  }
  
  DEBUG_PRINTLN(F("[I2C] PatchMatrix written to ADG2188"));
}

void writeAuditToADG2188(byte col, byte row) {
  if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) return;
  
  byte auditRow = activeState.patchMatrix[row] | (1 << col);
  writeToADG2188Row(ADG2188_MAIN_ROUTER_ADDR, row, auditRow);
  
  DEBUG_PRINTF("[I2C] Audit written: Row %d = 0x%02X\n", row, auditRow);
}

void writeToADG2188(byte address, byte data) {
  Wire.beginTransmission(address);
  Wire.write(data);
  Wire.endTransmission();
}

void writeToADG2188Row(byte address, byte row, byte data) {
  Wire.beginTransmission(address);
  Wire.write(row);
  Wire.write(data);
  Wire.endTransmission();
}

// ============================================================================
// EEPROM PERSISTENCE
// ============================================================================

void savePresetToEEPROM(byte presetIndex) {
  if (presetIndex >= NUM_PRESETS) return;
  
  uint16_t eepromAddress = presetIndex * (MATRIX_BYTES + 4);
  
  for (int i = 0; i < MATRIX_BYTES; i++) {
    EEPROM.write(eepromAddress + i, activeState.patchMatrix[i]);
  }
  
  DEBUG_PRINTF("[EEPROM] Preset %d saved\n", presetIndex);
  lastEEPROMSaveTime = millis();
}

void loadPresetFromEEPROM(byte presetIndex) {
  if (presetIndex >= NUM_PRESETS) return;
  
  uint16_t eepromAddress = presetIndex * (MATRIX_BYTES + 4);
  
  for (int i = 0; i < MATRIX_BYTES; i++) {
    activeState.patchMatrix[i] = EEPROM.read(eepromAddress + i);
  }
  
  currentPresetIndex = presetIndex;
  writePatchMatrixToADG2188();
  
  DEBUG_PRINTF("[EEPROM] Preset %d loaded\n", presetIndex);
  flagDisplayUpdate = true;
}

// ============================================================================
// DISPLAY RENDERING
// ============================================================================

void updateDisplay() {
  display.clearDisplay();
  
  if (uiMode == ROUTING_MODE) {
    renderRoutingMode();
  } else if (uiMode == MENU_MODE) {
    renderMenuMode();
  }
  
  display.display();
}

void renderRoutingMode() {
  // Render centered 8x8 grid
  for (int y = 0; y < MATRIX_SIZE; y++) {
    for (int x = 0; x < MATRIX_SIZE; x++) {
      renderMatrixBox(x, y);
    }
  }
  
  // Render status text in the right margin
  uint16_t statusX = GRID_START_X + GRID_PIXEL_SIZE + 3;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(statusX, 0);
  
  char label[8];
  sprintf(label, "%c:%d", 'A' + cursorX, cursorY + 1);
  display.println(label);
  
  display.setCursor(statusX, 10);
  display.printf("P:%d", currentPresetIndex);
}

void renderMatrixBox(byte x, byte y) {
  uint16_t boxX = GRID_START_X + (x * (BOX_SIZE + BOX_SPACING));
  uint16_t boxY = GRID_START_Y + (y * (BOX_SIZE + BOX_SPACING));
  
  bool isActive = activeState.getEntry(y, x);
  bool isAudit = isAuditioning && (auditingX == x) && (auditingY == y);
  
  if (isActive) {
    // 100% filled (active in patchMatrix)
    display.fillRect(boxX, boxY, BOX_SIZE, BOX_SIZE, SSD1306_WHITE);
  } else if (isAudit) {
    // 75% filled with outline (audition mode)
    for (int py = 0; py < BOX_SIZE - 2; py++) {
      for (int px = 0; px < BOX_SIZE - 2; px++) {
        if ((px + py) % 2 == 0) {
          display.drawPixel(boxX + 1 + px, boxY + 1 + py, SSD1306_WHITE);
        }
      }
    }
    // Draw outline
    display.drawRect(boxX, boxY, BOX_SIZE, BOX_SIZE, SSD1306_WHITE);
  } else {
    // 0% (off)
    display.drawRect(boxX, boxY, BOX_SIZE, BOX_SIZE, SSD1306_WHITE);
  }
}

void renderMenuMode() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("=== MENU MODE ==="));
  display.println(F("Turn encoder to switch presets"));
  display.printf("Current: %d\n", currentPresetIndex);
  display.println();
  display.println(F("Hold BTN2 to exit menu"));
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void printDebugStatus() {
  if (!SERIAL_DEBUG_ENABLED) return;
  
  DEBUG_PRINTLN(F("\n=== SYSTEM STATUS ==="));
  DEBUG_PRINTF("UI Mode: %s\n", (uiMode == ROUTING_MODE) ? "ROUTING" : "MENU");
  DEBUG_PRINTF("Cursor: X=%d Y=%d\n", cursorX, cursorY);
  DEBUG_PRINTF("Audition: %s (X=%d Y=%d)\n", 
               isAuditioning ? "ON" : "OFF", auditingX, auditingY);
  DEBUG_PRINTF("Preset: %d\n", currentPresetIndex);
  
  DEBUG_PRINT(F("PatchMatrix: "));
  for (int i = 0; i < MATRIX_BYTES; i++) {
    DEBUG_PRINTF("0x%02X ", activeState.patchMatrix[i]);
  }
  DEBUG_PRINTLN();
}
