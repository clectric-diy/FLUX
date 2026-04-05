/*
  ============================================================================
  NEXUS LUNETTA - 8x8 Analog Switch Matrix with Generative Engine
  ============================================================================
  FIRMWARE VARIANT: Router + Lunetta-style bitwise chaos engine
  
  This variant extends the base router with a generative rhythm engine inspired
  by the "Lunetta" modular synthesis approach - using simple logic gates and
  XOR operations to create chaotic but musicality patterns.
  
  Features:
  - All router functionality (signal routing + 8x8 matrix)
  - Generative XOR/AND/NAND/SHIFT rhythm patterns
  - LFSR (Linear Feedback Shift Register) sequencing
  - Rungler-style random pattern generator
  - Tempo control via encoder, BPM display
  - Menu mode for selecting generative algorithm
  
  Hardware: ATmega4809-A (TQFP-48) @ 5V logic
  Ecosystem: AE Modular standard (0-5V signals)
  
  INTERACTION:
  - All ROUTING_MODE controls work identically to router
  - MENU_MODE now has: Encoder 1 (Y) = select generative mode
                       Encoder 2 (X) = adjust BPM/tempo
  - Modes available: OFF, XOR-RHYTHM, AND-RHYTHM, NAND-RHYTHM, SHIFT-RHYTHM, LFSR, RUNGLER
  
  SHARED CODE:
  All I2C, EEPROM, display, and preset functions are in ../core/nexus-core.cpp/.h
  This file extends the router with generative logic.
  ============================================================================
*/

#include "../core/nexus-core.h"

// ============================================================================
// LUNETTA GENERATIVE ENGINE DEFINITIONS
// ============================================================================

#define ENABLE_LUNETTA_GENERATIVE 1

// Generative mode enumeration
enum GenerativeMode {
  MODE_OFF,           // No generative logic (pure router)
  MODE_XOR_RHYTHM,    // XOR-based rhythm generator
  MODE_AND_RHYTHM,    // AND-based rhythm generator
  MODE_NAND_RHYTHM,   // NAND-based rhythm generator
  MODE_SHIFT_RHYTHM,  // Shift-register rhythm
  MODE_LFSR,          // Linear Feedback Shift Register
  MODE_RUNGLER        // Rungler-inspired random sequences
};

// Generative state variables
GenerativeMode generativeMode = MODE_OFF;
byte generativeBPM = 120;           // Tempo in beats per minute
unsigned long generativeTickMs = 0; // Timestamp for rhythm generator
byte lfsrState = 0x01;              // LFSR internal state (must not be 0)
byte runglerCounter = 0;            // Rungler counter/sequencer position

// Interval between generative ticks (milliseconds)
// Formula: (60000 / BPM) / 4 = sixteenth-note interval
unsigned long getGenerativeInterval() {
  return (60000 / generativeBPM) / 4;
}

// ============================================================================
// GENERATIVE ENGINE FUNCTIONS
// ============================================================================

void updateGenerativeLogic() {
  // Main generative engine update - called each loop
  
  if (generativeMode == MODE_OFF) return;
  
  unsigned long now = millis();
  unsigned long interval = getGenerativeInterval();
  
  if ((now - generativeTickMs) < interval) return;
  generativeTickMs = now;
  
  // Execute generative algorithm based on selected mode
  switch (generativeMode) {
    case MODE_XOR_RHYTHM:
      updateXORRhythm();
      break;
    case MODE_AND_RHYTHM:
      updateANDRhythm();
      break;
    case MODE_NAND_RHYTHM:
      updateNANDRhythm();
      break;
    case MODE_SHIFT_RHYTHM:
      updateShiftRhythm();
      break;
    case MODE_LFSR:
      updateLFSR();
      break;
    case MODE_RUNGLER:
      updateRungler();
      break;
    default:
      break;
  }
  
  flagDisplayUpdate = true;
}

void updateXORRhythm() {
  // XOR-based chaos: toggle entries based on XOR logic
  // Creates rhythm patterns by XORing random positions
  
  static byte counter = 0;
  
  // XOR a pseudo-random row with a pseudo-random column
  byte rowToMutate = (counter ^ (counter >> 1)) % MATRIX_SIZE;
  byte colToMutate = ((counter * 13) ^ (counter >> 2)) % MATRIX_SIZE;
  
  // Toggle this entry with some probability
  if ((counter & 0x03) == 0) {  // Mutate every 4 ticks
    activeState.toggleEntry(rowToMutate, colToMutate);
    flagSaveNeeded = true;
    writePatchMatrixToADG2188();
  }
  
  counter++;
  
  DEBUG_PRINTF("[LUNETTA] XOR mutation at %d,%d\n", rowToMutate, colToMutate);
}

void updateANDRhythm() {
  // AND-based pattern: apply AND logic to matrix positions
  // Creates more stable/predictable patterns than XOR
  
  static byte counter = 0;
  
  byte rowToMutate = counter % MATRIX_SIZE;
  byte colToMutate = (counter / MATRIX_SIZE) % MATRIX_SIZE;
  
  // Apply AND logic: only keep switches that satisfy condition
  if ((rowToMutate & colToMutate) > 0) {
    activeState.setEntry(rowToMutate, colToMutate, true);
  } else {
    activeState.setEntry(rowToMutate, colToMutate, false);
  }
  
  counter++;
  if (counter >= (MATRIX_SIZE * MATRIX_SIZE)) counter = 0;
  
  flagSaveNeeded = true;
  writePatchMatrixToADG2188();
  
  DEBUG_PRINTF("[LUNETTA] AND pattern at %d,%d\n", rowToMutate, colToMutate);
}

void updateNANDRhythm() {
  // NAND-based pattern: inverted AND logic
  // Creates inverted/complementary patterns
  
  static byte counter = 0;
  
  byte rowToMutate = counter % MATRIX_SIZE;
  byte colToMutate = (counter / MATRIX_SIZE) % MATRIX_SIZE;
  
  // Apply NAND logic: inverted AND
  if ((rowToMutate & colToMutate) == 0) {
    activeState.setEntry(rowToMutate, colToMutate, true);
  } else {
    activeState.setEntry(rowToMutate, colToMutate, false);
  }
  
  counter++;
  if (counter >= (MATRIX_SIZE * MATRIX_SIZE)) counter = 0;
  
  flagSaveNeeded = true;
  writePatchMatrixToADG2188();
  
  DEBUG_PRINTF("[LUNETTA] NAND pattern at %d,%d\n", rowToMutate, colToMutate);
}

void updateShiftRhythm() {
  // Shift-register pattern: rotate/shift bits through matrix
  // Creates rhythmic patterns from bit shifting
  
  static byte shiftPattern = 0x01;
  
  for (int row = 0; row < MATRIX_SIZE; row++) {
    byte newRow = (shiftPattern << row) | (shiftPattern >> (8 - row));
    
    for (int col = 0; col < MATRIX_SIZE; col++) {
      bool bitState = bitRead(newRow, col);
      activeState.setEntry(row, col, bitState);
    }
  }
  
  shiftPattern = (shiftPattern << 1) | (shiftPattern >> 7);
  
  flagSaveNeeded = true;
  writePatchMatrixToADG2188();
  
  DEBUG_PRINTF("[LUNETTA] Shift pattern: 0x%02X\n", shiftPattern);
}

void updateLFSR() {
  // Linear Feedback Shift Register: pseudo-random sequence
  // Uses Galois/XOR LFSR for 8-bit maximal-length sequence
  
  // Tap positions for 8-bit LFSR: x^8 + x^6 + x^5 + x^4 + 1
  byte lsb = lfsrState & 1;
  lfsrState >>= 1;
  if (lsb) {
    lfsrState ^= 0xB8;  // Taps at bits 7,5,4,3
  }
  
  // Use LFSR state to set matrix pattern
  for (int row = 0; row < MATRIX_SIZE; row++) {
    byte rowPattern = (lfsrState ^ (row * 0x55)) & 0xFF;
    
    for (int col = 0; col < MATRIX_SIZE; col++) {
      bool bitState = bitRead(rowPattern, col);
      activeState.setEntry(row, col, bitState);
    }
  }
  
  flagSaveNeeded = true;
  writePatchMatrixToADG2188();
  
  DEBUG_PRINTF("[LUNETTA] LFSR state: 0x%02X\n", lfsrState);
}

void updateRungler() {
  // Rungler: combines counter and shift register for rhythmic variation
  // Inspired by the Rungler module in AE Modular
  
  static byte runglerShift = 0x01;
  
  // Rungler algorithm: use counter to index into shift pattern
  byte currentPattern = runglerShift ^ runglerCounter;
  
  for (int row = 0; row < MATRIX_SIZE; row++) {
    byte rowPattern = (currentPattern ^ (row << 1)) & 0xFF;
    
    for (int col = 0; col < MATRIX_SIZE; col++) {
      bool bitState = bitRead(rowPattern, col);
      activeState.setEntry(row, col, bitState);
    }
  }
  
  runglerCounter++;
  runglerShift = (runglerShift << 1) | (runglerShift >> 7);
  
  flagSaveNeeded = true;
  writePatchMatrixToADG2188();
  
  DEBUG_PRINTF("[LUNETTA] Rungler: counter=%d, shift=0x%02X\n", runglerCounter, runglerShift);
}

// ============================================================================
// MENU MODE HANDLERS (LUNETTA-SPECIFIC)
// ============================================================================

void handleMenuEncoder1(int8_t delta) {
  // Encoder 1 in menu mode: select generative mode
  
  if (delta > 0) {
    generativeMode = (GenerativeMode)((generativeMode + 1) % 7);
  } else if (delta < 0) {
    generativeMode = (GenerativeMode)((generativeMode - 1 + 7) % 7);
  }
  
  DEBUG_PRINTF("[LUNETTA] Generative mode: %d\n", generativeMode);
  flagDisplayUpdate = true;
}

void handleMenuEncoder2(int8_t delta) {
  // Encoder 2 in menu mode: adjust BPM
  
  if (delta > 0) {
    generativeBPM = min(generativeBPM + 5, 240);
  } else if (delta < 0) {
    generativeBPM = max(generativeBPM - 5, 30);
  }
  
  DEBUG_PRINTF("[LUNETTA] BPM: %d\n", generativeBPM);
  flagDisplayUpdate = true;
}

// ============================================================================
// AUDITION & ROUTING STATE (EXTENDED FROM ROUTER)
// ============================================================================

void startAuditioning(byte x, byte y) {
  // Begin audition at specified matrix position
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
  // End audition and restore patchMatrix to hardware
  isAuditioning = false;
  auditingX = 0;
  auditingY = 0;
  writePatchMatrixToADG2188();
  
  DEBUG_PRINTLN(F("[AUDITION] Stopped - patchMatrix restored"));
  flagDisplayUpdate = true;
}

void updateMatrixAuditioning() {
  // Called each loop to maintain audition state
  
  if (!isAuditioning) return;
  
  bool isEntryActive = activeState.getEntry(auditingY, auditingX);
  if (!isEntryActive) {
    writeAuditToADG2188(auditingX, auditingY);
  } else {
    writePatchMatrixToADG2188();
  }
}

// ============================================================================
// ENCODER INPUT PROCESSING
// ============================================================================

void processEncoderInput() {
  if (encoder1Delta != 0) {
    DEBUG_PRINTF("[ENC1] Delta: %d\n", encoder1Delta);
    
    if (uiMode == ROUTING_MODE) {
      // Y-axis movement: update cursor position and start audition
      int newY = (int)cursorY + encoder1Delta;
      newY = constrain(newY, 0, MATRIX_SIZE - 1);
      if (newY != cursorY) {
        cursorY = newY;
        startAuditioning(cursorX, cursorY);
        flagDisplayUpdate = true;
      }
    } else if (uiMode == MENU_MODE) {
      handleMenuEncoder1(encoder1Delta);
    }
    
    encoder1Delta = 0;
  }
  
  if (encoder2Delta != 0) {
    DEBUG_PRINTF("[ENC2] Delta: %d\n", encoder2Delta);
    
    if (uiMode == ROUTING_MODE) {
      // X-axis movement: update cursor position and start audition
      int newX = (int)cursorX + encoder2Delta;
      newX = constrain(newX, 0, MATRIX_SIZE - 1);
      if (newX != cursorX) {
        cursorX = newX;
        startAuditioning(cursorX, cursorY);
        flagDisplayUpdate = true;
      }
    } else if (uiMode == MENU_MODE) {
      handleMenuEncoder2(encoder2Delta);
    }
    
    encoder2Delta = 0;
  }
  
  // Check for button presses
  checkEncoderButtons();
}

// ============================================================================
// ENCODER BUTTON HANDLING
// ============================================================================

void checkEncoderButtons() {
  unsigned long now = millis();
  
  // Check Encoder 1 button (confirmation in ROUTING_MODE)
  static bool encoder1Down = false;
  static unsigned long encoder1PressTime = 0;
  
  bool encoder1State = digitalRead(ENCODER1_BTN);
  if (!encoder1State && !encoder1Down) {
    // Button just pressed
    encoder1Down = true;
    encoder1PressTime = now;
    DEBUG_PRINTLN(F("[BTN1] Pressed"));
  } else if (encoder1State && encoder1Down) {
    // Button just released
    encoder1Down = false;
    unsigned long pressDuration = now - encoder1PressTime;
    
    if (pressDuration < 500) {
      // Short press: toggle matrix entry
      if (uiMode == ROUTING_MODE) {
        activeState.toggleEntry(auditingY, auditingX);
        flagSaveNeeded = true;
        flagDisplayUpdate = true;
        DEBUG_PRINTF("[BTN1] Toggle X:%d Y:%d\n", auditingX, auditingY);
        writePatchMatrixToADG2188();
      }
    } else {
      // Long press: reserved for future use
      DEBUG_PRINTLN(F("[BTN1] Long press"));
    }
  }
  
  // Check Encoder 2 button (menu mode entry)
  static bool encoder2Down = false;
  static unsigned long encoder2PressTime = 0;
  
  bool encoder2State = digitalRead(ENCODER2_BTN);
  if (!encoder2State && !encoder2Down) {
    // Button just pressed
    encoder2Down = true;
    encoder2PressTime = now;
    DEBUG_PRINTLN(F("[BTN2] Pressed"));
  } else if (encoder2State && !encoder2Down) {
    // Button just released
    encoder2Down = false;
    unsigned long pressDuration = now - encoder2PressTime;
    
    if (pressDuration >= 3000) {
      // 3-second press: toggle UI mode
      flagModeChange = true;
      DEBUG_PRINTLN(F("[BTN2] Long press (3s) - mode change"));
    }
  }
}

void handleModeChange() {
  // Toggle between ROUTING_MODE and MENU_MODE
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

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  // Initialize serial for debugging
  if (SERIAL_DEBUG_ENABLED) {
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 3000) {
      delay(10);
    }
    DEBUG_PRINTLN(F("\n=== NEXUS LUNETTA STARTING ==="));
    DEBUG_PRINTLN(F("VARIANT: Router + Bitwise Generative Engine"));
  }
  
  // Initialize I2C bus
  Wire.begin();
  Wire.setClock(400000);
  DEBUG_PRINTLN(F("[SETUP] I2C bus initialized @ 400 kHz"));
  
  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    DEBUG_PRINTLN(F("[ERROR] SSD1306 OLED failed to initialize!"));
  } else {
    DEBUG_PRINTLN(F("[SETUP] SSD1306 OLED initialized"));
    display.setRotation(1);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("NEXUS LUNETTA"));
    display.println(F("Initializing..."));
    display.display();
  }
  
  // Initialize encoder pins as inputs
  pinMode(ENCODER1_A, INPUT_PULLUP);
  pinMode(ENCODER1_B, INPUT_PULLUP);
  pinMode(ENCODER1_BTN, INPUT_PULLUP);
  pinMode(ENCODER2_A, INPUT_PULLUP);
  pinMode(ENCODER2_B, INPUT_PULLUP);
  pinMode(ENCODER2_BTN, INPUT_PULLUP);
  
  DEBUG_PRINTLN(F("[SETUP] Encoder pins configured"));
  
  // Attach encoder interrupts
  attachInterrupt(digitalPinToInterrupt(ENCODER1_A), isr_encoder1Tick, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER2_A), isr_encoder2Tick, CHANGE);
  
  DEBUG_PRINTLN(F("[SETUP] Encoder interrupts attached"));
  
  // Initialize ADG2188 switches via I2C
  initializeADG2188();
  
  // Load last active preset from EEPROM
  loadPresetFromEEPROM(0);
  
  // Initialize LFSR with non-zero seed
  lfsrState = 0x55;
  
  DEBUG_PRINTLN(F("[SETUP] EEPROM state loaded"));
  DEBUG_PRINTLN(F("[SETUP] Generative engine initialized"));
  
  // Update display with initial state
  flagDisplayUpdate = true;
  
  DEBUG_PRINTLN(F("[SETUP] Initialization complete\n"));
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  unsigned long now = millis();
  
  // Process encoder movements and button presses
  processEncoderInput();
  
  // Handle UI mode changes
  if (flagModeChange) {
    flagModeChange = false;
    handleModeChange();
  }
  
  // Audition timeout check
  if (isAuditioning && (now - auditTimeoutMs > AUDITION_TIMEOUT_MS)) {
    DEBUG_PRINTLN(F("[AUDITION] Timeout - ending audition"));
    stopAuditioning();
  }
  
  // Update matrix audition if in routing mode
  if (uiMode == ROUTING_MODE) {
    updateMatrixAuditioning();
  }
  
  // Update generative engine (only in ROUTING_MODE for continuous generation)
  if (uiMode == ROUTING_MODE && generativeMode != MODE_OFF) {
    updateGenerativeLogic();
  }
  
  // Opportunistic EEPROM save
  if (flagSaveNeeded && (now - lastEEPROMSaveTime > EEPROM_SAVE_DELAY)) {
    flagSaveNeeded = false;
    savePresetToEEPROM(0);
    DEBUG_PRINTF("[EEPROM] Saved at %lu ms\n", now);
  }
  
  // Update display
  if (flagDisplayUpdate) {
    flagDisplayUpdate = false;
    updateDisplay();
  }
  
  // Small delay to prevent CPU spinning
  delay(10);
}

// ============================================================================
// INTERRUPT SERVICE ROUTINES (ISRs)
// ============================================================================

void isr_encoder1Tick() {
  static byte lastState = 0;
  byte currentState = (digitalRead(ENCODER1_A) << 1) | digitalRead(ENCODER1_B);
  
  if (digitalRead(ENCODER1_A) != digitalRead(ENCODER1_B)) {
    encoder1Delta++;
  } else {
    encoder1Delta--;
  }
}

void isr_encoder2Tick() {
  static byte lastState = 0;
  byte currentState = (digitalRead(ENCODER2_A) << 1) | digitalRead(ENCODER2_B);
  
  if (digitalRead(ENCODER2_A) != digitalRead(ENCODER2_B)) {
    encoder2Delta++;
  } else {
    encoder2Delta--;
  }
}

// ============================================================================
// END OF SKETCH
// ============================================================================
