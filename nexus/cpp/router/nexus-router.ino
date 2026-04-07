/*
  ============================================================================
  Nexus Router - 8x8 Analog Switch Matrix Router
  ============================================================================
  FIRMWARE VARIANT: Base router functionality
  
  This is the core router firmware for the Nexus module - a programmable
  8x8 analog switch matrix that can route any input to any output.
  Perfect for creating complex signal paths, audio routing, CV mixing,
  and modular synthesis patching.
  
  Features:
  - 8x8 analog switch matrix (ADG2188)
  - OLED display for visual feedback
  - Rotary encoders for navigation and control
  - EEPROM storage for presets
  - I2C communication for matrix control
  
  Hardware: Arduino Nano Every prototype (ATmega4809) @ 5V logic
  Ecosystem: AE Modular standard (0-5V signals)
  
  INTERACTION:
  - ROUTING_MODE: Turn encoders to select input/output, click to connect
  - MENU_MODE: Browse and load presets, adjust settings
  - OLED shows current matrix state and menu options
  
  SHARED CODE:
  All I2C, EEPROM, display, and preset functions are in ../core/nexus-core.cpp/.h
  This file implements the base router interface.
  ============================================================================
*/

#include "../core/nexus-core.h"

// ============================================================================
// ROUTER-SPECIFIC DEFINITIONS
// ============================================================================

#define ENABLE_ROUTER_MODE 1

// ============================================================================
// MAIN ARDUINO FUNCTIONS
// ============================================================================

void setup() {
  nexus_setup();
}

void loop() {
  nexus_loop();
}
