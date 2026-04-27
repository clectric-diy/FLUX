# Effects firmware — user guide

The **effects** build is a small **scaffold** that routes an internal test oscillator through a selectable process. It is useful for bring-up, experimenting with Spark audio I/O, and as a base for a larger FX program.

## Controls (current behavior)

| Control | Function |
| ------- | -------- |
| **Encoder (turn)** | Cycles the **effect model**: **Bypass** or **Overdrive** (as implemented in the source). |
| **K1** | **Oscillator frequency** (roughly 20 Hz–2 kHz, logarithmic). |
| **K2** | **Mix** between dry oscillator and processed sound (0–100%). In overdrive, drive depth tracks the mix in this scaffold. |

**SW1** and **SW2** are not given special functions in the current `effects.cpp` main loop; they remain available for future use.

## Saving

Parameters are **saved to QSPI** a few seconds after the last change, same pattern as the other Spark firmwares.

## Related

- [README](README.md) — build and flash  
- [Hardware](hardware.md) — front panel map
