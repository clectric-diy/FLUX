/*
  ============================================================================
  File: nexus-core.cpp
  ============================================================================
  Purpose
  -------
  This source file implements the shared Nexus runtime.
  It is the single place where hardware behavior and UI flow are executed.

  What lives here
  ---------------
  1) Runtime state definitions declared in nexus-core.h
  2) Input handling (encoders and buttons)
  3) Display rendering for routing and menu screens
  4) Routing switch writes over I2C (ADG2188)
  5) Preset save/load behavior with memory
  6) Main entry points: `nexus_setup()` and `nexus_loop()`

  How to use it
  -------------
  - Keep declarations in nexus-core.h and implementations in this file.
  - Add shared runtime behavior here so all Nexus sketches stay consistent.
  - Use `nexus_setup()` and `nexus_loop()` as the standard runtime entry points.
  ============================================================================
*/

#include "nexus-core.h"

void nexusDebugPrintf(const char* format, ...) {
  char buffer[96];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Serial.print(buffer);
}

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

namespace {

byte detectedOledAddress = OLED_I2C_ADDR;
constexpr uint32_t I2C_CLOCK_HZ = 100000;

const __FlashStringHelper* getDebugLevelName() {
#if SERIAL_DEBUG_LEVEL == 0
  return F("OFF");
#elif SERIAL_DEBUG_LEVEL == 1
  return F("ERROR");
#elif SERIAL_DEBUG_LEVEL == 2
  return F("INFO");
#elif SERIAL_DEBUG_LEVEL == 3
  return F("VERBOSE");
#else
  return F("CUSTOM");
#endif
}

bool i2cDevicePresent(byte address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

byte detectOledAddress() {
  if (i2cDevicePresent(0x3C)) {
    return 0x3C;
  }
  if (i2cDevicePresent(0x3D)) {
    return 0x3D;
  }
  return OLED_I2C_ADDR;
}

enum FillMode : uint8_t {
  FILL_25,
  FILL_100,
};

void printFormattedToDisplay(const char* format, ...) {
  char buffer[32];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  display.print(buffer);
}

void formatCursorPosition(char* out, size_t outLen, byte x, byte y) {
  snprintf(out, outLen, "%c:%d", 'A' + x, y + 1);
}

int16_t oledX(int16_t x) {
  return x + OLED_PIXEL_X_OFFSET;
}

int16_t oledY(int16_t y) {
  return y + OLED_PIXEL_Y_OFFSET;
}

void drawStippleRect(int16_t x, int16_t y, int16_t w, int16_t h, FillMode mode) {
  if (mode == FILL_100) {
    display.drawBox(x, y, w, h);
    return;
  }

  for (int16_t py = 0; py < h; py++) {
    for (int16_t px = 0; px < w; px++) {
      int16_t gx = x + px;
      int16_t gy = y + py;
      bool on = false;

      if (mode == FILL_25) {
        on = ((gx & 1) == 0) && ((gy & 1) == 0);  // 1/4 pixels on
      }

      if (on) {
        display.drawPixel(gx, gy);
      }
    }
  }
}

} // namespace

Patch activePatch;
Patch presetPatches[NUM_PRESETS];
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

unsigned long lastMemorySaveTime = 0;

namespace {

constexpr unsigned long BUTTON_DEBOUNCE_MS = 12;
constexpr unsigned long BUTTON_LONG_PRESS_MS = 3000;

struct DebouncedButton {
  enum Role : uint8_t {
    NAVIGATION_BUTTON,
    ACTION_BUTTON,
  };

  uint8_t pin;
  const char* name;
  Role role;
  bool rawState;
  bool stableState;
  unsigned long lastRawChangeMs;
  unsigned long pressStartMs;
  bool longHandled;

  void begin(uint8_t buttonPin, const char* buttonName, Role buttonRole) {
    pin = buttonPin;
    name = buttonName;
    role = buttonRole;
    rawState = digitalRead(pin);
    stableState = rawState;
    lastRawChangeMs = millis();
    pressStartMs = 0;
    longHandled = false;
  }
};

DebouncedButton encoder1Button;
DebouncedButton encoder2Button;

void movePresetSelectionBySteps(int8_t delta) {
  while (delta > 0) {
    nextPreset();
    delta--;
  }

  while (delta < 0) {
    prevPreset();
    delta++;
  }
}

void routerHandleMenuEncoder1(int8_t delta) {
  movePresetSelectionBySteps(delta);
}

void routerHandleMenuEncoder2(int8_t delta) {
  routerHandleMenuEncoder1(delta);
}

void startAuditioning(byte x, byte y) {
  auditingX = x;
  auditingY = y;
  isAuditioning = true;
  auditTimeoutMs = millis();

  bool isEntryActive = activePatch.isPatchConnectionActive(y, x);
  if (!isEntryActive) {
    writeAuditToRoutingSwitch(x, y);
  }

  char pos[8];
  formatCursorPosition(pos, sizeof(pos), x, y);
  DEBUG_PRINTF("[AUDITION] %s (Active:%d)\n", pos, isEntryActive);
  flagDisplayUpdate = true;
}

void stopAuditioning() {
  isAuditioning = false;
  auditingX = 0;
  auditingY = 0;
  writeActivePatchToRoutingSwitches();

  DEBUG_PRINTLN(F("[AUDITION] Stopped - active connections restored"));
  flagDisplayUpdate = true;
}

void logSelectionState(byte x, byte y) {
  char pos[8];
  formatCursorPosition(pos, sizeof(pos), x, y);
  bool isConnected = activePatch.isPatchConnectionActive(y, x);
  DEBUG_PRINTF("[SELECT] %s = %d (Row %d: 0x%02X)\n", pos, isConnected, y, activePatch.patchConnections[y]);
}

void traceEncoderPins() {
#if SERIAL_DEBUG_LEVEL >= 3
  static bool initialized = false;
  static uint8_t lastE2A = HIGH;
  static uint8_t lastE2B = HIGH;
  static uint8_t lastE2Btn = HIGH;
  static uint8_t lastE1A = HIGH;
  static uint8_t lastE1B = HIGH;
  static uint8_t lastE1Btn = HIGH;

  uint8_t e2a = digitalRead(ENCODER2_A);
  uint8_t e2b = digitalRead(ENCODER2_B);
  uint8_t e2btn = digitalRead(ENCODER2_BTN);
  uint8_t e1a = digitalRead(ENCODER1_A);
  uint8_t e1b = digitalRead(ENCODER1_B);
  uint8_t e1btn = digitalRead(ENCODER1_BTN);

  if (!initialized) {
    initialized = true;
    lastE2A = e2a;
    lastE2B = e2b;
    lastE2Btn = e2btn;
    lastE1A = e1a;
    lastE1B = e1b;
    lastE1Btn = e1btn;
    return;
  }

  if (e2a != lastE2A || e2b != lastE2B || e2btn != lastE2Btn || e1a != lastE1A || e1b != lastE1B || e1btn != lastE1Btn) {
    DEBUG_VERBOSE_PRINTF("[PINS] E2(A:%d B:%d SW:%d) E1(A:%d B:%d SW:%d)\n", e2a, e2b, e2btn, e1a, e1b, e1btn);
    lastE2A = e2a;
    lastE2B = e2b;
    lastE2Btn = e2btn;
    lastE1A = e1a;
    lastE1B = e1b;
    lastE1Btn = e1btn;
  }
#endif
}

void handleActionButtonShortPress() {
  if (uiMode != ROUTING_MODE) {
    return;
  }

  activePatch.togglePatchConnection(cursorY, cursorX);
  flagSaveNeeded = true;
  flagDisplayUpdate = true;
  logSelectionState(cursorX, cursorY);
  writeActivePatchToRoutingSwitches();
}

void updateButton(DebouncedButton& button, unsigned long now) {
  bool reading = digitalRead(button.pin);

  if (reading != button.rawState) {
    button.rawState = reading;
    button.lastRawChangeMs = now;
    DEBUG_VERBOSE_PRINTF("[%s RAW] %d\n", button.name, reading);
  }

  if ((now - button.lastRawChangeMs) < BUTTON_DEBOUNCE_MS) {
    return;
  }

  if (button.stableState != button.rawState) {
    button.stableState = button.rawState;

    if (button.stableState == LOW) {
      button.pressStartMs = now;
      button.longHandled = false;
      DEBUG_VERBOSE_PRINTF("[%s] Pressed\n", button.name);
    } else {
      unsigned long pressDuration = now - button.pressStartMs;
      DEBUG_VERBOSE_PRINTF("[%s] Released after %lu ms\n", button.name, pressDuration);

      if (!button.longHandled) {
        DEBUG_VERBOSE_PRINTF("[%s] Click\n", button.name);
      }

      if (button.role == DebouncedButton::ACTION_BUTTON && !button.longHandled) {
        handleActionButtonShortPress();
      }
    }
  }

  if (button.stableState == LOW && !button.longHandled && (now - button.pressStartMs) >= BUTTON_LONG_PRESS_MS) {
    button.longHandled = true;

    if (button.role == DebouncedButton::ACTION_BUTTON) {
      flagModeChange = true;
      DEBUG_PRINTLN(F("[BTN2] Long press (3s) - mode change"));
    }
  }
}

void updateMatrixAuditioning() {
  if (!isAuditioning) {
    return;
  }

  bool isEntryActive = activePatch.isPatchConnectionActive(auditingY, auditingX);
  if (!isEntryActive) {
    writeAuditToRoutingSwitch(auditingX, auditingY);
  } else {
    writeActivePatchToRoutingSwitches();
  }
}

void handleModeChange() {
  if (uiMode == ROUTING_MODE) {
    uiMode = MENU_MODE;
    stopAuditioning();
    DEBUG_PRINTLN(F("[MODE] -> MENU_MODE"));
  } else {
    uiMode = ROUTING_MODE;
    DEBUG_PRINTLN(F("[MODE] -> ROUTING_MODE"));
  }

  flagDisplayUpdate = true;
}

void checkEncoderButtons() {
  unsigned long now = millis();

  updateButton(encoder1Button, now);
  updateButton(encoder2Button, now);
}

void isr_encoder1Tick() {
  if (digitalRead(ENCODER1_A) != digitalRead(ENCODER1_B)) {
    encoder1Delta++;
  } else {
    encoder1Delta--;
  }
}

void isr_encoder2Tick() {
  if (digitalRead(ENCODER2_A) != digitalRead(ENCODER2_B)) {
    encoder2Delta--;
  } else {
    encoder2Delta++;
  }
}

void processEncoderInput() {
  traceEncoderPins();

  int8_t horizontalDelta = encoder1Delta;
  if (horizontalDelta != 0) {
    if (uiMode == ROUTING_MODE) {
      int newX = constrain((int) cursorX + horizontalDelta, 0, MATRIX_SIZE - 1);
      if (newX != cursorX) {
        cursorX = newX;
        startAuditioning(cursorX, cursorY);
        flagDisplayUpdate = true;
      }
    } else {
      routerHandleMenuEncoder1(horizontalDelta);
    }

    encoder1Delta = 0;
  }

  int8_t verticalDelta = encoder2Delta;
  if (verticalDelta != 0) {
    if (uiMode == ROUTING_MODE) {
      int newY = constrain((int) cursorY + verticalDelta, 0, MATRIX_SIZE - 1);
      if (newY != cursorY) {
        cursorY = newY;
        startAuditioning(cursorX, cursorY);
        flagDisplayUpdate = true;
      }
    } else {
      routerHandleMenuEncoder2(verticalDelta);
    }

    encoder2Delta = 0;
  }

  checkEncoderButtons();
}

void initializeDisplay(const __FlashStringHelper* title) {
  detectedOledAddress = detectOledAddress();
  display.setI2CAddress(detectedOledAddress << 1);
  display.begin();

  DEBUG_PRINTF("[SETUP] OLED initialized (1.3in SH1106 profile), addr=0x%02X\n", detectedOledAddress);
  display.setFont(u8g2_font_5x8_tr);
  display.clearBuffer();
  display.sendBuffer();
  delay(20);

  display.clearBuffer();
  display.setCursor(oledX(0), oledY(0));
  display.println(title);
  display.println(F("Initializing..."));
  display.sendBuffer();
}

} // namespace

Patch& presetPatchAt(byte idx) {
  if (idx >= NUM_PRESETS) {
    idx = 0;
  }
  return presetPatches[idx];
}

void loadActivePatchFromPreset(byte idx) {
  activePatch = presetPatchAt(idx);
  currentPresetIndex = idx;
  DEBUG_PRINTF("[PRESET] Loaded preset %d\n", idx);
}

void saveActivePatchToPreset(byte idx) {
  presetPatchAt(idx) = activePatch;
  currentPresetIndex = idx;
  savePresetToMemory(idx);
  DEBUG_PRINTF("[PRESET] Saved active to preset %d\n", idx);
}

void nextPreset() {
  currentPresetIndex = (currentPresetIndex + 1) % NUM_PRESETS;
  loadPresetFromMemory(currentPresetIndex);
  DEBUG_PRINTF("[PRESET] Next -> %d\n", currentPresetIndex);
  flagDisplayUpdate = true;
}

void prevPreset() {
  if (currentPresetIndex == 0) {
    currentPresetIndex = NUM_PRESETS - 1;
  } else {
    currentPresetIndex--;
  }

  loadPresetFromMemory(currentPresetIndex);
  DEBUG_PRINTF("[PRESET] Prev -> %d\n", currentPresetIndex);
  flagDisplayUpdate = true;
}

void initializeRoutingSwitches() {
  DEBUG_PRINTLN(F("[I2C] Initializing routing switches (ADG2188)..."));

  writeToSwitch(INPUT_SWITCH_I2C_ADDR, 0x00);
  writeToSwitch(GENERATED_SWITCH_I2C_ADDR, 0x00);

  for (int row = 0; row < MATRIX_BYTES; row++) {
    writeSwitchRegister(ROUTING_SWITCH_I2C_ADDR, row, 0x00);
  }

  DEBUG_PRINTLN(F("[I2C] Routing switch initialization complete (ADG2188)"));
}

void writeActivePatchToRoutingSwitches() {
  for (int row = 0; row < MATRIX_BYTES; row++) {
    writeSwitchRegister(ROUTING_SWITCH_I2C_ADDR, row, activePatch.patchConnections[row]);
  }

  DEBUG_PRINTLN(F("[I2C] Active patch written to routing switches (ADG2188)"));
}

void writeAuditToRoutingSwitch(byte col, byte row) {
  if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) {
    return;
  }

  byte auditRow = activePatch.patchConnections[row] | (1 << col);
  writeSwitchRegister(ROUTING_SWITCH_I2C_ADDR, row, auditRow);
}

void writeToSwitch(byte address, byte data) {
  Wire.beginTransmission(address);
  Wire.write(data);
  Wire.endTransmission();
}

void writeSwitchRegister(byte address, byte registerIndex, byte data) {
  Wire.beginTransmission(address);
  Wire.write(registerIndex);
  Wire.write(data);
  Wire.endTransmission();
}

void savePresetToMemory(byte presetIndex) {
  if (presetIndex >= NUM_PRESETS) {
    return;
  }

  uint16_t memoryAddress = MEMORY_PRESET_BASE_ADDR + (presetIndex * MEMORY_PRESET_STRIDE);

  for (int index = 0; index < MATRIX_BYTES; index++) {
    EEPROM.update(memoryAddress + index, activePatch.patchConnections[index]);
  }

  // Write the initialization marker so future loads know the data is valid.
  EEPROM.update(MEMORY_INIT_MARKER_ADDR, MEMORY_INIT_MARKER_VAL);

  DEBUG_PRINTF("[MEMORY] Preset %d saved\n", presetIndex);
  lastMemorySaveTime = millis();
}

void saveActivePatchToMemory() {
  for (int index = 0; index < MATRIX_BYTES; index++) {
    EEPROM.update(MEMORY_ACTIVE_PATCH_ADDR + index, activePatch.patchConnections[index]);
  }

  // Write the initialization marker so future loads know the data is valid.
  EEPROM.update(MEMORY_INIT_MARKER_ADDR, MEMORY_INIT_MARKER_VAL);

  DEBUG_PRINTLN(F("[MEMORY] Active patch saved"));
  lastMemorySaveTime = millis();
}

void loadPresetFromMemory(byte presetIndex) {
  if (presetIndex >= NUM_PRESETS) {
    return;
  }

  // If memory has never been written, default to blank rather than loading garbage
  if (EEPROM.read(MEMORY_INIT_MARKER_ADDR) != MEMORY_INIT_MARKER_VAL) {
    DEBUG_PRINTLN(F("[MEMORY] No valid data - defaulting to blank"));
    activePatch.clear();
    currentPresetIndex = presetIndex;
    writeActivePatchToRoutingSwitches();
    flagDisplayUpdate = true;
    return;
  }

  uint16_t memoryAddress = MEMORY_PRESET_BASE_ADDR + (presetIndex * MEMORY_PRESET_STRIDE);

  for (int index = 0; index < MATRIX_BYTES; index++) {
    activePatch.patchConnections[index] = EEPROM.read(memoryAddress + index);
  }

  currentPresetIndex = presetIndex;
  writeActivePatchToRoutingSwitches();

  DEBUG_PRINTF("[MEMORY] Preset %d loaded\n", presetIndex);
  flagDisplayUpdate = true;
}

void loadActivePatchFromMemory() {
  // If memory has never been written, default to blank rather than loading garbage.
  if (EEPROM.read(MEMORY_INIT_MARKER_ADDR) != MEMORY_INIT_MARKER_VAL) {
    DEBUG_PRINTLN(F("[MEMORY] No valid active patch - defaulting to blank"));
    activePatch.clear();
    writeActivePatchToRoutingSwitches();
    flagDisplayUpdate = true;
    return;
  }

  for (int index = 0; index < MATRIX_BYTES; index++) {
    activePatch.patchConnections[index] = EEPROM.read(MEMORY_ACTIVE_PATCH_ADDR + index);
  }

  writeActivePatchToRoutingSwitches();
  DEBUG_PRINTLN(F("[MEMORY] Active patch loaded"));
  flagDisplayUpdate = true;
}

void updateDisplay() {
  display.clearBuffer();

  if (uiMode == ROUTING_MODE) {
    renderRoutingMode();
  } else if (uiMode == MENU_MODE) {
    renderMenuMode();
  }

  display.sendBuffer();
}

void renderRoutingMode() {
  for (int y = 0; y < MATRIX_SIZE; y++) {
    for (int x = 0; x < MATRIX_SIZE; x++) {
      renderMatrixBox(x, y);
    }
  }

  uint16_t statusX = GRID_START_X + GRID_PIXEL_SIZE + 3;
  display.setFont(u8g2_font_5x8_tr);
  display.setCursor(oledX(statusX), oledY(0));

  char label[8];
  formatCursorPosition(label, sizeof(label), cursorX, cursorY);
  display.println(label);

  display.setCursor(oledX(statusX), oledY(10));
  printFormattedToDisplay("P:%d", currentPresetIndex + 1);

  display.setCursor(oledX(statusX), oledY(20));
  display.println(isAuditioning ? F("AUD") : F("   "));
}

void renderMatrixBox(byte x, byte y) {
  uint16_t boxX = oledX(GRID_START_X + (x * (BOX_SIZE + BOX_SPACING)));
  uint16_t boxY = oledY(GRID_START_Y + (y * (BOX_SIZE + BOX_SPACING)));

  const bool isSelected = activePatch.isPatchConnectionActive(y, x);
  const bool isCursorCell = (cursorX == x) && (cursorY == y);

  // Keep state visibility stable while moving cursor:
  // - unselected: 25% stipple
  // - selected: 100% fill
  // - cursor: outline only (does not change fill state)
  FillMode mode = isSelected ? FILL_100 : FILL_25;

  drawStippleRect(boxX, boxY, BOX_SIZE, BOX_SIZE, mode);

  if (isCursorCell) {
    // Open square outline around the 6x6 cell
    display.drawFrame(boxX - 1, boxY - 1, BOX_SIZE + 2, BOX_SIZE + 2);
  }
}

void renderMenuMode() {
  display.setFont(u8g2_font_5x8_tr);
  display.setCursor(oledX(0), oledY(0));
  display.println(F("=== MENU MODE ==="));
  display.println(F("Turn encoder to switch presets"));
  printFormattedToDisplay("Current: %d\n", currentPresetIndex);
  display.println();
  display.println(F("Hold BTN2 to exit menu"));
}

void printDebugStatus() {
  if (!SERIAL_DEBUG_ENABLED) {
    return;
  }

  DEBUG_PRINTLN(F("\n=== SYSTEM STATUS ==="));
  DEBUG_PRINTF("UI Mode: %s\n", (uiMode == ROUTING_MODE) ? "ROUTING" : "MENU");
  DEBUG_PRINTF("Cursor: X=%d Y=%d\n", cursorX, cursorY);
  DEBUG_PRINTF("Audition: %s (X=%d Y=%d)\n", isAuditioning ? "ON" : "OFF", auditingX, auditingY);
  DEBUG_PRINTF("Preset: %d\n", currentPresetIndex);

  DEBUG_PRINT(F("Connections by row: "));
  for (int index = 0; index < MATRIX_BYTES; index++) {
    DEBUG_PRINTF("0x%02X ", activePatch.patchConnections[index]);
  }
  DEBUG_PRINTLN();
}

void nexus_setup() {
  if (SERIAL_DEBUG_ENABLED) {
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 3000) {
      delay(10);
    }
    DEBUG_PRINTLN(F("\n=== NEXUS ROUTER STARTING ==="));
    DEBUG_PRINTLN(F("VARIANT: Base Router"));
  #if defined(ARDUINO_AVR_NANO_EVERY)
    DEBUG_PRINTLN(F("BOARD: Arduino Nano Every (ATMega4809)"));
  #else
    DEBUG_PRINTLN(F("BOARD: Not Nano Every macro"));
  #endif
    DEBUG_PRINT(F("DEBUG LEVEL: "));
    DEBUG_PRINTLN(getDebugLevelName());
    DEBUG_PRINTF("ENC MAP: E1(A=%d,B=%d,SW=%d) E2(A=%d,B=%d,SW=%d)\n",
                 ENCODER1_A, ENCODER1_B, ENCODER1_BTN,
                 ENCODER2_A, ENCODER2_B, ENCODER2_BTN);
  }

  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);
  DEBUG_PRINTLN(F("[SETUP] I2C bus initialized @ 100 kHz"));

  initializeDisplay(F("NEXUS ROUTER"));

  pinMode(ENCODER1_A, INPUT_PULLUP);
  pinMode(ENCODER1_B, INPUT_PULLUP);
  pinMode(ENCODER1_BTN, INPUT_PULLUP);
  pinMode(ENCODER2_A, INPUT_PULLUP);
  pinMode(ENCODER2_B, INPUT_PULLUP);
  pinMode(ENCODER2_BTN, INPUT_PULLUP);
  DEBUG_PRINTLN(F("[SETUP] Encoder pins configured"));
  DEBUG_PRINTF("[SETUP] BTN levels E1:%d E2:%d (0=pressed,1=released)\n", digitalRead(ENCODER1_BTN), digitalRead(ENCODER2_BTN));
  encoder1Button.begin(ENCODER1_BTN, "BTN1", DebouncedButton::NAVIGATION_BUTTON);
  encoder2Button.begin(ENCODER2_BTN, "BTN2", DebouncedButton::ACTION_BUTTON);

  attachInterrupt(digitalPinToInterrupt(ENCODER1_A), isr_encoder1Tick, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER2_A), isr_encoder2Tick, CHANGE);

  DEBUG_PRINTLN(F("[SETUP] Encoder interrupts attached"));

  initializeRoutingSwitches();
  loadActivePatchFromMemory();
  DEBUG_PRINTLN(F("[SETUP] Active patch restored from memory (or defaulted to blank)"));

  flagDisplayUpdate = true;
  DEBUG_PRINTLN(F("[SETUP] Initialization complete\n"));
}

void nexus_loop() {
  unsigned long now = millis();

  processEncoderInput();

  if (flagModeChange) {
    flagModeChange = false;
    handleModeChange();
  }

  if (isAuditioning && (now - auditTimeoutMs > AUDITION_TIMEOUT_MS)) {
    DEBUG_PRINTLN(F("[AUDITION] Timeout - ending audition"));
    stopAuditioning();
  }

  if (uiMode == ROUTING_MODE) {
    updateMatrixAuditioning();
  }

  if (flagSaveNeeded && (now - lastMemorySaveTime > MEMORY_SAVE_DELAY)) {
    flagSaveNeeded = false;
    saveActivePatchToMemory();
    DEBUG_PRINTF("[MEMORY] Saved at %lu ms\n", now);
  }

  if (flagDisplayUpdate) {
    flagDisplayUpdate = false;
    updateDisplay();
  }

  delay(1);
}
