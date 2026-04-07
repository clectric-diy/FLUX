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

byte detectedOledAddress = OLED_ADDR;

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
  return OLED_ADDR;
}

enum FillMode : uint8_t {
  FILL_25,
  FILL_75,
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
  return x + OLED_PIXEL_OFFSET_X;
}

int16_t oledY(int16_t y) {
  return y + OLED_PIXEL_OFFSET_Y;
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
      } else if (mode == FILL_75) {
        on = !(((gx & 1) == 1) && ((gy & 1) == 1)); // 3/4 pixels on
      }

      if (on) {
        display.drawPixel(gx, gy);
      }
    }
  }
}

} // namespace

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

namespace {

constexpr unsigned long BUTTON_DEBOUNCE_MS = 12;
constexpr unsigned long BUTTON_LONG_PRESS_MS = 3000;

struct DebouncedButton {
  uint8_t pin;
  const char* name;
  bool rawState;
  bool stableState;
  unsigned long lastRawChangeMs;
  unsigned long pressStartMs;
  bool longHandled;

  void begin(uint8_t buttonPin, const char* buttonName) {
    pin = buttonPin;
    name = buttonName;
    rawState = digitalRead(pin);
    stableState = rawState;
    lastRawChangeMs = millis();
    pressStartMs = 0;
    longHandled = false;
  }
};

DebouncedButton encoder1Button;
DebouncedButton encoder2Button;

void routerHandleMenuEncoder1(int8_t delta) {
  if (delta > 0) {
    nextPreset();
  } else if (delta < 0) {
    prevPreset();
  }
}

void routerHandleMenuEncoder2(int8_t delta) {
  routerHandleMenuEncoder1(delta);
}

void startAuditioning(byte x, byte y) {
  auditingX = x;
  auditingY = y;
  isAuditioning = true;
  auditTimeoutMs = millis();

  bool isEntryActive = activeState.getEntry(y, x);
  if (!isEntryActive) {
    writeAuditToADG2188(x, y);
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
  writePatchMatrixToADG2188();

  DEBUG_PRINTLN(F("[AUDITION] Stopped - patchMatrix restored"));
  flagDisplayUpdate = true;
}

void logSelectionState(byte x, byte y) {
  char pos[8];
  formatCursorPosition(pos, sizeof(pos), x, y);
  bool state = activeState.getEntry(y, x);
  DEBUG_PRINTF("[SELECT] %s = %d (Row %d: 0x%02X)\n", pos, state, y, activeState.patchMatrix[y]);
}

void traceEncoderPins() {
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
    DEBUG_PRINTF("[PINS] E2(A:%d B:%d SW:%d) E1(A:%d B:%d SW:%d)\n", e2a, e2b, e2btn, e1a, e1b, e1btn);
    lastE2A = e2a;
    lastE2B = e2b;
    lastE2Btn = e2btn;
    lastE1A = e1a;
    lastE1B = e1b;
    lastE1Btn = e1btn;
  }
}

void handleEncoder2ShortPress() {
  if (uiMode != ROUTING_MODE) {
    return;
  }

  activeState.toggleEntry(cursorY, cursorX);
  flagSaveNeeded = true;
  flagDisplayUpdate = true;
  logSelectionState(cursorX, cursorY);
  writePatchMatrixToADG2188();
}

void updateButton(DebouncedButton& button, unsigned long now) {
  bool reading = digitalRead(button.pin);

  if (reading != button.rawState) {
    button.rawState = reading;
    button.lastRawChangeMs = now;
    DEBUG_PRINTF("[%s RAW] %d\n", button.name, reading);
  }

  if ((now - button.lastRawChangeMs) < BUTTON_DEBOUNCE_MS) {
    return;
  }

  if (button.stableState != button.rawState) {
    button.stableState = button.rawState;

    if (button.stableState == LOW) {
      button.pressStartMs = now;
      button.longHandled = false;
      DEBUG_PRINTF("[%s] Pressed\n", button.name);
    } else {
      unsigned long pressDuration = now - button.pressStartMs;
      DEBUG_PRINTF("[%s] Released after %lu ms\n", button.name, pressDuration);

      if (!button.longHandled) {
        DEBUG_PRINTF("[%s] Click\n", button.name);
      }

      if (&button == &encoder2Button && !button.longHandled) {
        handleEncoder2ShortPress();
      }
    }
  }

  if (button.stableState == LOW && !button.longHandled && (now - button.pressStartMs) >= BUTTON_LONG_PRESS_MS) {
    button.longHandled = true;

    if (&button == &encoder2Button) {
      flagModeChange = true;
      DEBUG_PRINTLN(F("[BTN2] Long press (3s) - mode change"));
    }
  }
}

void updateMatrixAuditioning() {
  if (!isAuditioning) {
    return;
  }

  bool isEntryActive = activeState.getEntry(auditingY, auditingX);
  if (!isEntryActive) {
    writeAuditToADG2188(auditingX, auditingY);
  } else {
    writePatchMatrixToADG2188();
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
    encoder2Delta++;
  } else {
    encoder2Delta--;
  }
}

void processEncoderInput() {
  traceEncoderPins();

  if (encoder1Delta != 0) {
    if (uiMode == ROUTING_MODE) {
      int newX = constrain((int) cursorX + encoder1Delta, 0, MATRIX_SIZE - 1);
      if (newX != cursorX) {
        cursorX = newX;
        startAuditioning(cursorX, cursorY);
        flagDisplayUpdate = true;
      }
    } else {
      routerHandleMenuEncoder1(encoder1Delta);
    }

    encoder1Delta = 0;
  }

  if (encoder2Delta != 0) {
    if (uiMode == ROUTING_MODE) {
      int newY = constrain((int) cursorY + encoder2Delta, 0, MATRIX_SIZE - 1);
      if (newY != cursorY) {
        cursorY = newY;
        startAuditioning(cursorX, cursorY);
        flagDisplayUpdate = true;
      }
    } else {
      routerHandleMenuEncoder2(encoder2Delta);
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
  savePresetToEEPROM(currentPresetIndex);
  currentPresetIndex = (currentPresetIndex + 1) % NUM_PRESETS;
  loadPresetFromEEPROM(currentPresetIndex);
  DEBUG_PRINTF("[PRESET] Next -> %d\n", currentPresetIndex);
  flagDisplayUpdate = true;
}

void prevPreset() {
  savePresetToEEPROM(currentPresetIndex);

  if (currentPresetIndex == 0) {
    currentPresetIndex = NUM_PRESETS - 1;
  } else {
    currentPresetIndex--;
  }

  loadPresetFromEEPROM(currentPresetIndex);
  DEBUG_PRINTF("[PRESET] Prev -> %d\n", currentPresetIndex);
  flagDisplayUpdate = true;
}

void initializeADG2188() {
  DEBUG_PRINTLN(F("[I2C] Initializing ADG2188 switches..."));

  writeToADG2188(ADG2188_INPUT_MUX1_ADDR, 0x00);
  writeToADG2188(ADG2188_INPUT_MUX2_ADDR, 0x00);

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
  if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) {
    return;
  }

  byte auditRow = activeState.patchMatrix[row] | (1 << col);
  writeToADG2188Row(ADG2188_MAIN_ROUTER_ADDR, row, auditRow);
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

void savePresetToEEPROM(byte presetIndex) {
  if (presetIndex >= NUM_PRESETS) {
    return;
  }

  uint16_t eepromAddress = presetIndex * EEPROM_PRESET_STRIDE;

  for (int index = 0; index < MATRIX_BYTES; index++) {
    EEPROM.update(eepromAddress + index, activeState.patchMatrix[index]);
  }

  // Mark EEPROM as initialised so loadPresetFromEEPROM knows the data is valid
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);

  DEBUG_PRINTF("[EEPROM] Preset %d saved\n", presetIndex);
  lastEEPROMSaveTime = millis();
}

void loadPresetFromEEPROM(byte presetIndex) {
  if (presetIndex >= NUM_PRESETS) {
    return;
  }

  // If EEPROM has never been written, default to blank rather than loading garbage
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VAL) {
    DEBUG_PRINTLN(F("[EEPROM] No valid data - defaulting to blank"));
    activeState.clear();
    currentPresetIndex = presetIndex;
    writePatchMatrixToADG2188();
    flagDisplayUpdate = true;
    return;
  }

  uint16_t eepromAddress = presetIndex * EEPROM_PRESET_STRIDE;

  for (int index = 0; index < MATRIX_BYTES; index++) {
    activeState.patchMatrix[index] = EEPROM.read(eepromAddress + index);
  }

  currentPresetIndex = presetIndex;
  writePatchMatrixToADG2188();

  DEBUG_PRINTF("[EEPROM] Preset %d loaded\n", presetIndex);
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
  display.println(label);
}

void renderMatrixBox(byte x, byte y) {
  uint16_t boxX = oledX(GRID_START_X + (x * (BOX_SIZE + BOX_SPACING)));
  uint16_t boxY = oledY(GRID_START_Y + (y * (BOX_SIZE + BOX_SPACING)));

  const bool isSelected = activeState.getEntry(y, x);
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

  DEBUG_PRINT(F("PatchMatrix: "));
  for (int index = 0; index < MATRIX_BYTES; index++) {
    DEBUG_PRINTF("0x%02X ", activeState.patchMatrix[index]);
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
    DEBUG_PRINTLN(F("BOARD: Arduino Nano Every (ATmega4809)"));
  #else
    DEBUG_PRINTLN(F("BOARD: Not Nano Every macro"));
  #endif
    DEBUG_PRINTLN(F("ENC MAP: E2(A=14,B=15,SW=16) E1(A=17,B=20,SW=21)"));
  }

  Wire.begin();
  Wire.setClock(100000);
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
  encoder1Button.begin(ENCODER1_BTN, "BTN1");
  encoder2Button.begin(ENCODER2_BTN, "BTN2");

  attachInterrupt(digitalPinToInterrupt(ENCODER1_A), isr_encoder1Tick, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER2_A), isr_encoder2Tick, CHANGE);

  DEBUG_PRINTLN(F("[SETUP] Encoder interrupts attached"));

  initializeADG2188();
  loadPresetFromEEPROM(0);
  DEBUG_PRINTLN(F("[SETUP] State restored from EEPROM (or defaulted to blank)"));

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

  if (flagSaveNeeded && (now - lastEEPROMSaveTime > EEPROM_SAVE_DELAY)) {
    flagSaveNeeded = false;
    savePresetToEEPROM(currentPresetIndex);
    DEBUG_PRINTF("[EEPROM] Saved at %lu ms\n", now);
  }

  if (flagDisplayUpdate) {
    flagDisplayUpdate = false;
    updateDisplay();
  }

  delay(1);
}
