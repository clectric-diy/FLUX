#include "nexus-core.h"

void nexusDebugPrintf(const char* format, ...) {
  char buffer[96];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Serial.print(buffer);
}

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

namespace {

void printFormattedToDisplay(const char* format, ...) {
  char buffer[32];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  display.print(buffer);
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

  DEBUG_PRINTF("[AUDITION] Started at X:%d Y:%d (Active:%d)\n", x, y, isEntryActive);
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

  static bool encoder1Down = false;
  static unsigned long encoder1PressTime = 0;

  bool encoder1State = digitalRead(ENCODER1_BTN);
  if (!encoder1State && !encoder1Down) {
    encoder1Down = true;
    encoder1PressTime = now;
    DEBUG_PRINTLN(F("[BTN1] Pressed"));
  } else if (encoder1State && encoder1Down) {
    encoder1Down = false;
    unsigned long pressDuration = now - encoder1PressTime;

    if (pressDuration < 500 && uiMode == ROUTING_MODE) {
      activeState.toggleEntry(auditingY, auditingX);
      flagSaveNeeded = true;
      flagDisplayUpdate = true;
      DEBUG_PRINTF("[BTN1] Toggle X:%d Y:%d\n", auditingX, auditingY);
      writePatchMatrixToADG2188();
    } else if (pressDuration >= 500) {
      DEBUG_PRINTLN(F("[BTN1] Long press"));
    }
  }

  static bool encoder2Down = false;
  static unsigned long encoder2PressTime = 0;

  bool encoder2State = digitalRead(ENCODER2_BTN);
  if (!encoder2State && !encoder2Down) {
    encoder2Down = true;
    encoder2PressTime = now;
    DEBUG_PRINTLN(F("[BTN2] Pressed"));
  } else if (encoder2State && encoder2Down) {
    encoder2Down = false;
    unsigned long pressDuration = now - encoder2PressTime;

    if (pressDuration >= 3000) {
      flagModeChange = true;
      DEBUG_PRINTLN(F("[BTN2] Long press (3s) - mode change"));
    }
  }
}

void processEncoderInput() {
  if (encoder1Delta != 0) {
    DEBUG_PRINTF("[ENC1] Delta: %d\n", encoder1Delta);

    if (uiMode == ROUTING_MODE) {
      int newY = constrain((int) cursorY + encoder1Delta, 0, MATRIX_SIZE - 1);
      if (newY != cursorY) {
        cursorY = newY;
        startAuditioning(cursorX, cursorY);
        flagDisplayUpdate = true;
      }
    } else {
      routerHandleMenuEncoder1(encoder1Delta);
    }

    encoder1Delta = 0;
  }

  if (encoder2Delta != 0) {
    DEBUG_PRINTF("[ENC2] Delta: %d\n", encoder2Delta);

    if (uiMode == ROUTING_MODE) {
      int newX = constrain((int) cursorX + encoder2Delta, 0, MATRIX_SIZE - 1);
      if (newX != cursorX) {
        cursorX = newX;
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

void initializeDisplay(const __FlashStringHelper* title) {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    DEBUG_PRINTLN(F("[ERROR] SSD1309 OLED failed to initialize (SSD1306-compatible mode)!"));
    return;
  }

  DEBUG_PRINTLN(F("[SETUP] SSD1309 OLED initialized (SSD1306-compatible mode)"));
  display.setRotation(0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(title);
  display.println(F("Initializing..."));
  display.display();
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

void savePresetToEEPROM(byte presetIndex) {
  if (presetIndex >= NUM_PRESETS) {
    return;
  }

  uint16_t eepromAddress = presetIndex * (MATRIX_BYTES + 4);

  for (int index = 0; index < MATRIX_BYTES; index++) {
    EEPROM.write(eepromAddress + index, activeState.patchMatrix[index]);
  }

  DEBUG_PRINTF("[EEPROM] Preset %d saved\n", presetIndex);
  lastEEPROMSaveTime = millis();
}

void loadPresetFromEEPROM(byte presetIndex) {
  if (presetIndex >= NUM_PRESETS) {
    return;
  }

  uint16_t eepromAddress = presetIndex * (MATRIX_BYTES + 4);

  for (int index = 0; index < MATRIX_BYTES; index++) {
    activeState.patchMatrix[index] = EEPROM.read(eepromAddress + index);
  }

  currentPresetIndex = presetIndex;
  writePatchMatrixToADG2188();

  DEBUG_PRINTF("[EEPROM] Preset %d loaded\n", presetIndex);
  flagDisplayUpdate = true;
}

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
  for (int y = 0; y < MATRIX_SIZE; y++) {
    for (int x = 0; x < MATRIX_SIZE; x++) {
      renderMatrixBox(x, y);
    }
  }

  uint16_t statusX = GRID_START_X + GRID_PIXEL_SIZE + 3;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(statusX, 0);

  char label[8];
  snprintf(label, sizeof(label), "%c:%d", 'A' + cursorX, cursorY + 1);
  display.println(label);

  display.setCursor(statusX, 10);
  printFormattedToDisplay("P:%d", currentPresetIndex);
}

void renderMatrixBox(byte x, byte y) {
  uint16_t boxX = GRID_START_X + (x * (BOX_SIZE + BOX_SPACING));
  uint16_t boxY = GRID_START_Y + (y * (BOX_SIZE + BOX_SPACING));

  bool isActive = activeState.getEntry(y, x);
  bool isAudit = isAuditioning && (auditingX == x) && (auditingY == y);

  if (isActive) {
    display.fillRect(boxX, boxY, BOX_SIZE, BOX_SIZE, SSD1306_WHITE);
  } else if (isAudit) {
    for (int py = 0; py < BOX_SIZE - 2; py++) {
      for (int px = 0; px < BOX_SIZE - 2; px++) {
        if ((px + py) % 2 == 0) {
          display.drawPixel(boxX + 1 + px, boxY + 1 + py, SSD1306_WHITE);
        }
      }
    }
    display.drawRect(boxX, boxY, BOX_SIZE, BOX_SIZE, SSD1306_WHITE);
  } else {
    display.drawRect(boxX, boxY, BOX_SIZE, BOX_SIZE, SSD1306_WHITE);
  }
}

void renderMenuMode() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
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
  }

  Wire.begin();
  Wire.setClock(400000);
  DEBUG_PRINTLN(F("[SETUP] I2C bus initialized @ 400 kHz"));

  initializeDisplay(F("NEXUS ROUTER"));

  pinMode(ENCODER1_A, INPUT_PULLUP);
  pinMode(ENCODER1_B, INPUT_PULLUP);
  pinMode(ENCODER1_BTN, INPUT_PULLUP);
  pinMode(ENCODER2_A, INPUT_PULLUP);
  pinMode(ENCODER2_B, INPUT_PULLUP);
  pinMode(ENCODER2_BTN, INPUT_PULLUP);
  DEBUG_PRINTLN(F("[SETUP] Encoder pins configured"));

  attachInterrupt(digitalPinToInterrupt(ENCODER1_A), isr_encoder1Tick, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER2_A), isr_encoder2Tick, CHANGE);
  DEBUG_PRINTLN(F("[SETUP] Encoder interrupts attached"));

  initializeADG2188();
  loadPresetFromEEPROM(0);

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

  delay(10);
}
