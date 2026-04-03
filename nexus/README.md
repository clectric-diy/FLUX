# NEXUS Router: Pure Signal Control

**Master the matrix.** The Router variant gives you direct, hands-on control over NEXUS's 8×8 signal routing matrix. No algorithms, no automation—just pure, tactile patching that lets you explore signal flow in ways traditional cables never could.

## What It Does

Turn encoders to navigate the matrix grid on the OLED display. Each intersection represents a potential connection between inputs and outputs. Press buttons to create and break connections, auditioning your routing decisions in real-time.

## Creative Flow

Start simple: route a single oscillator through different filters. Then experiment with complex signal paths—feedback loops, parallel processing, dynamic reconfiguration. The Router teaches you matrix thinking, where every connection is a creative decision.

## Why Router First?

This is where you learn NEXUS's language. Understanding manual routing builds intuition for the generative variants. It's minimal, focused, and endlessly creative—perfect for exploring patching possibilities without distraction.

## Getting Started

The interface is intuitive: encoders move your cursor, buttons toggle connections. Eight preset slots let you save and recall your favorite routings. The OLED shows your matrix state at a glance.

**FLUX Variant:** Router  
**File:** `nexus-router.ino` (~356 lines)  
**Flash:** ~11 KB  
**RAM:** ~0.3 KB  
**FLUX Variant:** Router  
**File:** `nexus-router.ino` (~356 lines)  
**Flash:** ~11 KB  
**RAM:** ~0.3 KB  
**Difficulty:** Beginner-friendly  
**License:** CERN-OHL-S | **Community:** [clectric.diy](https://clectric.diy)

| Component | Pin | Type |
|-----------|-----|------|
| Encoder 1 A | 5 | Input |
| Encoder 1 B | 6 | Input |
| Encoder 1 Button | 7 | Input |
| Encoder 2 A | 8 | Input |
| Encoder 2 B | 9 | Input |
| Encoder 2 Button | 10 | Input |
| I2C SDA | PA2 | I2C |
| I2C SCL | PA3 | I2C |

## Customization

This is the perfect base for learning and extending:

1. **Add new modes**: Extend the UI state machine
2. **Add external clock input**: Connect clock to a pin and advance via external timing
3. **Add CV output**: Use PWM pins to generate control voltages based on matrix state
4. **Add MIDI support**: Use Serial to implement MIDI note/CC mapping

All shared hardware functions (I2C, EEPROM, display) are available via `#include "nexus-core.h"`
