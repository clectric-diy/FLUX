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
  1) Runtime variable definitions declared in nexus-core.h
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

// Small helper so we can keep printf-style debug logs in one place.
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

// Some SH1106-compatible displays answer at 0x3D instead of 0x3C.
byte detectedOledAddress = OLED_I2C_ADDR;
constexpr uint32_t I2C_CLOCK_HZ = 100000;

// Converts numeric debug level into a readable label for startup logs.
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

// Quick I2C probe: returns true when a device acknowledges this address.
bool i2cDevicePresent(byte address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

// Probes common SH1106 addresses and returns the detected OLED I2C address.
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
  // 25% stipple highlights the cursor row and column without looking "on".
  FILL_25,
  // 100% fill marks an active connection clearly.
  FILL_100,
};

// Small formatting helper for OLED text output.
void printFormattedToDisplay(const char* format, ...) {
  char buffer[32];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  display.print(buffer);
}

// Converts matrix coordinates to a human-friendly label (for example A:1).
void formatCursorPosition(char* out, size_t outLen, byte x, byte y) {
  snprintf(out, outLen, "%c:%d", 'A' + x, y + 1);
}

// Applies display X offset for SH1106-style frame buffer alignment.
int16_t oledX(int16_t x) {
  return x + OLED_PIXEL_X_OFFSET;
}

// Applies display Y offset for SH1106-style frame buffer alignment.
int16_t oledY(int16_t y) {
  return y + OLED_PIXEL_Y_OFFSET;
}

// Draws one cell using a simple fill pattern that suits a 1-bit OLED.
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
        on = ((gx & 1) == 0) && ((gy & 1) == 0);  // Checker pattern: ~25% of pixels on
      }

      if (on) {
        display.drawPixel(gx, gy);
      }
    }
  }
}

// Draws one shared dot at every grid intersection so adjacent cells share corners.
void drawSharedGridDots() {
  for (int y = 0; y <= MATRIX_SIZE; y++) {
    int16_t dotY = oledY(GRID_START_Y - 1 + (y * (BOX_SIZE + BOX_SPACING)));

    for (int x = 0; x <= MATRIX_SIZE; x++) {
      int16_t dotX = oledX(GRID_START_X - 1 + (x * (BOX_SIZE + BOX_SPACING)));
      display.drawPixel(dotX, dotY);
    }
  }
}

} // namespace

// Active patch = current full routing setup the user is editing/hearing.
Patch activePatch;
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

constexpr unsigned long BUTTON_DEBOUNCE_MS = 50;
constexpr unsigned long BUTTON_LONG_PRESS_MS = 3000;
// EC11 encoders produce 2 quadrature state changes per physical detent click.
constexpr int8_t ENCODER_TICKS_PER_DETENT = 2;

struct DebouncedButton {
  // Navigation button moves/selects. Action button toggles/changes mode.
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

  // Initializes one debounced button state block.
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

// Applies preset movement one step at a time so wrap behavior stays consistent.
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

// In menu mode, encoder 1 scrolls presets.
void routerHandleMenuEncoder1(int8_t delta) {
  movePresetSelectionBySteps(delta);
}

// In menu mode, encoder 2 mirrors preset scrolling for convenience.
void routerHandleMenuEncoder2(int8_t delta) {
  routerHandleMenuEncoder1(delta);
}

// Starts a temporary audition preview at the current cursor position.
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

// Stops audition preview and restores the full active patch.
void stopAuditioning() {
  isAuditioning = false;
  auditingX = 0;
  auditingY = 0;
  writeActivePatchToRoutingSwitches();

  DEBUG_PRINTLN(F("[AUDITION] Stopped - active connections restored"));
  flagDisplayUpdate = true;
}

// Logs one connection value and its containing row byte for debug tracing.
void logSelectionState(byte x, byte y) {
  char pos[8];
  formatCursorPosition(pos, sizeof(pos), x, y);
  bool isConnected = activePatch.isPatchConnectionActive(y, x);
  DEBUG_PRINTF("[SELECT] %s = %d (Row %d: 0x%02X)\n", pos, isConnected, y, activePatch.patchConnections[y]);
}

// Verbose-only pin tracing to debug encoder hardware behavior.
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

// Toggles the currently selected connection in the active patch.
void toggleSelectedConnection() {
  if (uiMode != ROUTING_MODE) {
    DEBUG_PRINTLN(F("[CLICK] Ignored (not in ROUTING_MODE)"));
    return;
  }

  activePatch.togglePatchConnection(cursorY, cursorX);
  flagSaveNeeded = true;
  flagDisplayUpdate = true;
  DEBUG_PRINTF("[CLICK] Toggled selected connection at %c:%d\n", 'A' + cursorX, cursorY + 1);
  logSelectionState(cursorX, cursorY);
  writeActivePatchToRoutingSwitches();
}

// Handles short press on the action button.
void handleActionButtonShortPress() {
  DEBUG_PRINTLN(F("[CLICK] BTN2 short press"));
  toggleSelectedConnection();
}

// Handles short press on the navigation encoder button.
void handleNavigationButtonShortPress() {
  DEBUG_PRINTLN(F("[CLICK] BTN1 short press"));
  toggleSelectedConnection();
}

// Debounces one button and fires click or long-press actions on state change.
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
      // Button pressed — record when it went down for long-press timing.
      button.pressStartMs = now;
      button.longHandled = false;
      DEBUG_VERBOSE_PRINTF("[%s] Pressed\n", button.name);
    } else {
      // Button released — fire short-click if a long-press did not already handle it.
      unsigned long pressDuration = now - button.pressStartMs;
      DEBUG_VERBOSE_PRINTF("[%s] Released after %lu ms\n", button.name, pressDuration);

      if (!button.longHandled) {
        DEBUG_PRINTF("[CLICK] %s\n", button.name);

        if (button.role == DebouncedButton::ACTION_BUTTON) {
          handleActionButtonShortPress();
        }

        if (button.role == DebouncedButton::NAVIGATION_BUTTON) {
          handleNavigationButtonShortPress();
        }
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

// Keeps the audition preview active while the cursor remains on the audition cell.
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

// Toggles UI mode between routing view and preset menu.
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

// Polls and debounces both encoder push buttons.
void checkEncoderButtons() {
  unsigned long now = millis();

  updateButton(encoder1Button, now);
  updateButton(encoder2Button, now);
}

// Quadrature state machine table.
// Rows = previous 2-bit state (A<<1|B). Columns = new 2-bit state.
// +1 = valid CW step, -1 = valid CCW step, 0 = no movement or invalid.
static const int8_t ENCODER_STATE_TABLE[4][4] = {
  { 0, -1, +1,  0 },  // prev 00
  { +1,  0,  0, -1 },  // prev 01
  { -1,  0,  0, +1 },  // prev 10
  { 0, +1, -1,  0 },  // prev 11
};

// Interrupt handler for encoder 1 — uses quadrature state machine to reject noise.
void isr_encoder1Tick() {
  static uint8_t lastState = 0;
  uint8_t newState = (digitalRead(ENCODER1_A) << 1) | digitalRead(ENCODER1_B);
  encoder1Delta += ENCODER_STATE_TABLE[lastState][newState];
  lastState = newState;
}

// Interrupt handler for encoder 2 — uses quadrature state machine to reject noise.
void isr_encoder2Tick() {
  static uint8_t lastState = 0;
  uint8_t newState = (digitalRead(ENCODER2_A) << 1) | digitalRead(ENCODER2_B);
  encoder2Delta += ENCODER_STATE_TABLE[lastState][newState];
  lastState = newState;
}

// Processes encoder movement and button events for routing/menu modes.
void processEncoderInput() {
  traceEncoderPins();

  // Convert raw encoder ticks into one UI step per physical detent.
  static int8_t encoder1TickAccumulator = 0;
  static int8_t encoder2TickAccumulator = 0;

  noInterrupts();
  int8_t rawEncoder1Ticks = encoder1Delta;
  int8_t rawEncoder2Ticks = encoder2Delta;
  encoder1Delta = 0;
  encoder2Delta = 0;
  interrupts();

  encoder1TickAccumulator += rawEncoder1Ticks;
  encoder2TickAccumulator += rawEncoder2Ticks;

  int8_t horizontalDelta = encoder1TickAccumulator / ENCODER_TICKS_PER_DETENT;
  encoder1TickAccumulator %= ENCODER_TICKS_PER_DETENT;
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
  }

  int8_t verticalDelta = encoder2TickAccumulator / ENCODER_TICKS_PER_DETENT;
  encoder2TickAccumulator %= ENCODER_TICKS_PER_DETENT;
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
  }

  checkEncoderButtons();
}

// Initializes the OLED and displays a brief startup message.
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

// Select next preset slot and load its patch from memory.
void nextPreset() {
  currentPresetIndex = (currentPresetIndex + 1) % NUM_PRESETS;
  loadPresetFromMemory(currentPresetIndex);
  DEBUG_PRINTF("[PRESET] Next -> %d\n", currentPresetIndex);
  flagDisplayUpdate = true;
}

// Select previous preset slot and load its patch from memory.
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

// Resets all three ADG2188 switch chips to a known blank startup state.
void initializeRoutingSwitches() {
  DEBUG_PRINTLN(F("[I2C] Initializing routing switches (ADG2188)..."));

  writeToSwitch(INPUT_SWITCH_I2C_ADDR, 0x00);
  writeToSwitch(GENERATED_SWITCH_I2C_ADDR, 0x00);

  for (int row = 0; row < MATRIX_BYTES; row++) {
    writeSwitchRegister(ROUTING_SWITCH_I2C_ADDR, row, 0x00);
  }

  DEBUG_PRINTLN(F("[I2C] Routing switch initialization complete (ADG2188)"));
}

// Writes the full active patch into the routing switch registers.
void writeActivePatchToRoutingSwitches() {
  for (int row = 0; row < MATRIX_BYTES; row++) {
    writeSwitchRegister(ROUTING_SWITCH_I2C_ADDR, row, activePatch.patchConnections[row]);
  }

  DEBUG_PRINTLN(F("[I2C] Active patch written to routing switches (ADG2188)"));
}

// Writes a temporary preview connection for one register during audition.
void writeAuditToRoutingSwitch(byte col, byte row) {
  if (row >= MATRIX_SIZE || col >= MATRIX_SIZE) {
    return;
  }

  byte auditRow = activePatch.patchConnections[row] | (1 << col);
  writeSwitchRegister(ROUTING_SWITCH_I2C_ADDR, row, auditRow);
}

// Single-byte write helper for simple switch commands.
void writeToSwitch(byte address, byte data) {
  Wire.beginTransmission(address);
  Wire.write(data);
  Wire.endTransmission();
}

// Writes one addressed register value to an ADG2188 switch chip.
void writeSwitchRegister(byte address, byte registerIndex, byte data) {
  Wire.beginTransmission(address);
  Wire.write(registerIndex);
  Wire.write(data);
  Wire.endTransmission();
}

// Saves the current active patch to memory.
void saveActivePatchToMemory() {
  for (int index = 0; index < MATRIX_BYTES; index++) {
    EEPROM.update(MEMORY_ACTIVE_PATCH_ADDR + index, activePatch.patchConnections[index]);
  }

  // Initialization marker tells future boots that memory contains valid patch data.
  EEPROM.update(MEMORY_INIT_MARKER_ADDR, MEMORY_INIT_MARKER_VAL);

  DEBUG_PRINTLN(F("[MEMORY] Active patch saved"));
  lastMemorySaveTime = millis();
}

// Loads one preset patch from memory into the active patch.
void loadPresetFromMemory(byte presetIndex) {
  if (presetIndex >= NUM_PRESETS) {
    return;
  }

  // If memory is uninitialized, use a blank patch instead of random bytes.
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

// Restores the last active patch from memory on startup.
void loadActivePatchFromMemory() {
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

// Render the current UI mode and flush the frame to the OLED.
void updateDisplay() {
  display.clearBuffer();

  if (uiMode == ROUTING_MODE) {
    renderRoutingMode();
  } else if (uiMode == MENU_MODE) {
    renderMenuMode();
  }

  display.sendBuffer();
}

// Draw the routing grid and right-side status labels.
void renderRoutingMode() {
  // drawSharedGridDots();

  for (int y = 0; y < MATRIX_SIZE; y++) {
    for (int x = 0; x < MATRIX_SIZE; x++) {
      renderMatrixBox(x, y);
    }
  }

  uint16_t statusX = GRID_START_X + GRID_PIXEL_SIZE + 3;
  display.setFont(u8g2_font_5x8_tr);
  display.setCursor(oledX(statusX), oledY(0));
  display.println(F("POS"));

  char label[8];
  formatCursorPosition(label, sizeof(label), cursorX, cursorY);
  display.setCursor(oledX(statusX), oledY(10));
  display.println(label);

  display.setCursor(oledX(statusX), oledY(20));
  display.println(isAuditioning ? F("AUD") : F("   "));
}

// Draws one matrix cell using fill + cursor-outline rules.
void renderMatrixBox(byte x, byte y) {
  uint16_t boxX = oledX(GRID_START_X + (x * (BOX_SIZE + BOX_SPACING)));
  uint16_t boxY = oledY(GRID_START_Y + (y * (BOX_SIZE + BOX_SPACING)));

  const bool isSelected = activePatch.isPatchConnectionActive(y, x);
  const bool isCursorCell = (cursorX == x) && (cursorY == y);

  // Visual rules:
  // - active connection: solid fill
  // - inactive connection: blank cell with shared corner dots drawn by the grid
  // - cursor: square outline only
  if (isSelected) {
    drawStippleRect(boxX, boxY, BOX_SIZE, BOX_SIZE, FILL_100);
  }

  if (isCursorCell) {
    // Open square outline around the 6x6 cell
    display.drawFrame(boxX - 1, boxY - 1, BOX_SIZE + 2, BOX_SIZE + 2);
  }
}

// Draws the preset-selection menu screen.
void renderMenuMode() {
  display.setFont(u8g2_font_5x8_tr);
  display.setCursor(oledX(0), oledY(0));
  display.println(F("=== MENU MODE ==="));
  display.println(F("Turn encoder to switch presets"));
  printFormattedToDisplay("Current: %d\n", currentPresetIndex);
  display.println();
  display.println(F("Hold BTN2 to exit menu"));
}

// One-time hardware/runtime initialization.
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

  // Attach interrupts on both A and B pins so the state machine sees every edge.
  attachInterrupt(digitalPinToInterrupt(ENCODER1_A), isr_encoder1Tick, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER1_B), isr_encoder1Tick, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER2_A), isr_encoder2Tick, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER2_B), isr_encoder2Tick, CHANGE);

  DEBUG_PRINTLN(F("[SETUP] Encoder interrupts attached"));

  initializeRoutingSwitches();
  loadActivePatchFromMemory();
  DEBUG_PRINTLN(F("[SETUP] Active patch restored from memory (or defaulted to blank)"));

  flagDisplayUpdate = true;
  DEBUG_PRINTLN(F("[SETUP] Initialization complete\n"));
}

// Main runtime loop: input, mode updates, audition timeout, autosave, redraw.
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
    // Delay writes so rapid edits do not wear memory unnecessarily.
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
