/*
  ============================================================================
  Nexus Sequencer - 8x8 Analog Switch Matrix with Step Sequencer
  ============================================================================
  FIRMWARE VARIANT: Router + 16-step pattern sequencer
  
  This variant extends the base router with a step sequencer for creating
  rhythmic routing patterns. Program 16 steps of matrix configurations,
  advance through them via internal or external clock, and create
  complex rhythmic patches.
  
  Features:
  - All router functionality (signal routing + 8x8 matrix)
  - 16-step sequencer with independent pattern storage
  - Step editing: turn encoder to select step, click to edit current step
  - Internal or external clock input (via future hardware)
  - Tempo control via encoder, BPM display
  - Swing/shuffle timing adjustment
  - Pattern storage/recall with EEPROM
  
  Hardware: Arduino Nano Every prototype (ATmega4809) @ 5V logic
  Ecosystem: AE Modular standard (0-5V signals)
  
  INTERACTION:
  - ROUTING_MODE: Edit the current step's matrix configuration
  - MENU_MODE: Select step (Encoder 1), adjust tempo (Encoder 2)
  - Clock input advances sequencer automatically
  
  SHARED CODE:
  All I2C, EEPROM, display, and preset functions are in ../nexus-core/nexus-core.cpp/.h
  This file extends the router with step sequencer logic.
  ============================================================================
*/

#include "../nexus-core/nexus-core.h"

// ============================================================================
// STEP SEQUENCER DEFINITIONS
// ============================================================================

#define ENABLE_STEP_SEQUENCER 1
#define SEQUENCER_STEPS 16

// Step sequence storage
// stepSeqMatrix[step][row] represents the patchMatrix configuration for that step
PresetState stepSeqMatrix[SEQUENCER_STEPS];

// Sequencer state variables
byte currentStep = 0;               // Current step being played (0-15)
byte editStep = 0;                  // Current step being edited (0-15)
byte sequencerBPM = 120;            // Tempo in beats per minute
byte sequencerSwing = 0;            // Swing percentage (0-100)
bool isSequencerRunning = false;    // Sequencer playback state
unsigned long sequencerTickMs = 0;  // Timestamp for step advance

// Mode for sequencer interaction
enum SequencerEditMode {
  SEQ_PLAY,       // Playing back sequence
  SEQ_EDIT_STEP,  // Editing step configuration
  SEQ_EDIT_TIMING // Editing tempo/swing
};

SequencerEditMode seqEditMode = SEQ_PLAY;

// ============================================================================
// STEP SEQUENCER FUNCTIONS
// ============================================================================

unsigned long getSequencerInterval() {
  // Calculate milliseconds per step
  // 16 steps per beat, so: (60000 / BPM) / 4 = sixteenth-note interval
  return (60000 / sequencerBPM) / 4;
}

void playbackSequencerStep(byte stepNum) {
  // Load the patchMatrix configuration for the specified step
  
  if (stepNum >= SEQUENCER_STEPS) return;
  
  activeState = stepSeqMatrix[stepNum];
  writePatchMatrixToADG2188();
  
  currentStep = stepNum;
  flagDisplayUpdate = true;
  
  DEBUG_PRINTF("[SEQUENCER] Step %d playback\n", stepNum);
}

void advanceSequencer() {
  // Advance to next step in sequence
  
  if (!isSequencerRunning) return;
  
  currentStep = (currentStep + 1) % SEQUENCER_STEPS;
  playbackSequencerStep(currentStep);
}

void updateSequencerPlayback() {
  // Called each loop to advance sequencer based on tempo
  
  if (!isSequencerRunning) return;
  
  unsigned long now = millis();
  unsigned long interval = getSequencerInterval();
  
  if ((now - sequencerTickMs) >= interval) {
    sequencerTickMs = now;
    advanceSequencer();
  }
}

void editSequencerStep(byte stepNum) {
  // Enter edit mode for specified step
  
  if (stepNum >= SEQUENCER_STEPS) return;
  
  editStep = stepNum;
  activeState = stepSeqMatrix[stepNum];
  seqEditMode = SEQ_EDIT_STEP;
  
  DEBUG_PRINTF("[SEQUENCER] Edit mode: Step %d\n", stepNum);
  flagDisplayUpdate = true;
}

void saveSequencerStep() {
  // Save current activeState back to step sequence
  
  stepSeqMatrix[editStep] = activeState;
  flagSaveNeeded = true;
  
  DEBUG_PRINTF("[SEQUENCER] Step %d saved\n", editStep);
}

void toggleSequencerPlayback() {
  // Start/stop sequencer playback
  
  isSequencerRunning = !isSequencerRunning;
  
  if (isSequencerRunning) {
    sequencerTickMs = millis();
    currentStep = 0;
    playbackSequencerStep(0);
    DEBUG_PRINTLN(F("[SEQUENCER] Playback started"));
  } else {
    DEBUG_PRINTLN(F("[SEQUENCER] Playback stopped"));
  }
  
  flagDisplayUpdate = true;
}

// ============================================================================
// MENU MODE HANDLERS (SEQUENCER-SPECIFIC)
// ============================================================================

void handleMenuEncoder1(int8_t delta) {
  // Encoder 1 in menu mode: select step to edit
  
  if (seqEditMode == SEQ_EDIT_STEP) {
    if (delta > 0) {
      editStep = (editStep + 1) % SEQUENCER_STEPS;
    } else if (delta < 0) {
      editStep = (editStep - 1 + SEQUENCER_STEPS) % SEQUENCER_STEPS;
    }
    
    // Load the selected step for editing
    activeState = stepSeqMatrix[editStep];
    
    DEBUG_PRINTF("[SEQUENCER] Edit step: %d\n", editStep);
    flagDisplayUpdate = true;
  } else if (seqEditMode == SEQ_EDIT_TIMING) {
    // Alternative: use for tempo control
    if (delta > 0) {
      sequencerBPM = min(sequencerBPM + 5, 240);
    } else if (delta < 0) {
      sequencerBPM = max(sequencerBPM - 5, 30);
    }
    
    DEBUG_PRINTF("[SEQUENCER] BPM: %d\n", sequencerBPM);
    flagDisplayUpdate = true;
  }
}

void handleMenuEncoder2(int8_t delta) {
  // Encoder 2 in menu mode: adjust tempo or swing
  
  if (delta > 0) {
    sequencerBPM = min(sequencerBPM + 5, 240);
  } else if (delta < 0) {
    sequencerBPM = max(sequencerBPM - 5, 30);
  }
  
  DEBUG_PRINTF("[SEQUENCER] BPM: %d\n", sequencerBPM);
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
  
  // Check Encoder 1 button (confirmation in ROUTING_MODE, step edit in MENU_MODE)
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
      // Short press: toggle matrix entry (in routing mode) or save step (in menu)
      if (uiMode == ROUTING_MODE) {
        activeState.toggleEntry(auditingY, auditingX);
        flagSaveNeeded = true;
        flagDisplayUpdate = true;
        DEBUG_PRINTF("[BTN1] Toggle X:%d Y:%d\n", auditingX, auditingY);
        writePatchMatrixToADG2188();
        
        // If we're editing a step in the sequencer, save the change
        if (seqEditMode == SEQ_EDIT_STEP) {
          saveSequencerStep();
        }
      }
    } else {
      // Long press: enter step edit mode or toggle playback
      if (uiMode == MENU_MODE) {
        toggleSequencerPlayback();
      }
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
    seqEditMode = SEQ_EDIT_STEP;
    editSequencerStep(0);  // Start editing from step 0
    DEBUG_PRINTLN(F("[MODE] -> MENU_MODE"));
  } else {
    uiMode = ROUTING_MODE;
    if (isSequencerRunning) {
      playbackSequencerStep(currentStep);
    }
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
    DEBUG_PRINTLN(F("\n=== Nexus Sequencer Starting ==="));
    DEBUG_PRINTLN(F("VARIANT: Router + 16-Step Sequencer"));
  }
  
  // Initialize I2C bus
  Wire.begin();
  Wire.setClock(400000);
  DEBUG_PRINTLN(F("[SETUP] I2C bus initialized @ 400 kHz"));
  
  // Initialize OLED display (SSD1309 module via SSD1306-compatible commands)
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    DEBUG_PRINTLN(F("[ERROR] SSD1309 OLED failed to initialize (SSD1306-compatible mode)!"));
  } else {
    DEBUG_PRINTLN(F("[SETUP] SSD1309 OLED initialized (SSD1306-compatible mode)"));
    display.setRotation(0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("Nexus Sequencer"));
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
  
  // Load last active preset from EEPROM (step 0)
  loadPresetFromEEPROM(0);
  stepSeqMatrix[0] = activeState;
  
  // Initialize remaining sequencer steps to empty
  for (int i = 1; i < SEQUENCER_STEPS; i++) {
    stepSeqMatrix[i].clear();
  }
  
  DEBUG_PRINTLN(F("[SETUP] Sequencer initialized"));
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
  
  // Update sequencer playback (only if running)
  if (isSequencerRunning) {
    updateSequencerPlayback();
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
