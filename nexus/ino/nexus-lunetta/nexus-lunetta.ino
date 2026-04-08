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
  
  Hardware: Arduino Nano Every prototype (ATmega4809) @ 5V logic
  Ecosystem: AE Modular standard (0-5V signals)
  
  INTERACTION:
  - All ROUTING_MODE controls work identically to router
  - MENU_MODE now has: Encoder 1 (Y) = select generative mode
                       Encoder 2 (X) = adjust BPM/tempo
  - Modes available: OFF, XOR-RHYTHM, AND-RHYTHM, NAND-RHYTHM, SHIFT-RHYTHM, LFSR, RUNGLER
  
  SHARED CODE:
  Shared hardware, display, EEPROM, and router primitives live in the bundled
  nexus-core Arduino library at ../libraries/nexus-core/.
  This file extends the router with generative logic.

  NOTE:
  Variant-specific behavior is intentionally deferred until nexus-core is
  finalized. This sketch remains a thin wrapper around core runtime entry points.
*/

#include <nexus-core.h>

void setup() {
  nexus_setup();
}

void loop() {
  nexus_loop();
}
