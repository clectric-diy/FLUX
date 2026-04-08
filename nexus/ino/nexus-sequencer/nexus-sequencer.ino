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
  Shared hardware, display, EEPROM, and router primitives live in the bundled
  nexus-core Arduino library at ../libraries/nexus-core/.
  This file extends the router with step sequencer logic.

  NOTE:
  Variant-specific behavior is intentionally deferred until nexus-core is
  finalized. This sketch remains a thin wrapper around core runtime entry points.
  ============================================================================
*/

#include <nexus-core.h>

void setup() {
  nexus_setup();
}

void loop() {
  nexus_loop();
}
