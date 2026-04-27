# Filters firmware — user guide

The **filters** build is a **scaffold** that runs an internal saw test tone through a simple **multimode** filter so you can hear lowpass, highpass, and bandpass characters on the module.

## Controls (current behavior)

| Control | Function |
| ------- | -------- |
| **Encoder (turn)** | Cycles **filter type**: **Lowpass**, **Highpass**, or **Bandpass**. |
| **K1** | **Oscillator frequency** (roughly 20 Hz–2 kHz, logarithmic) — the test tone coming *into* the filter. |
| **K2** | **Cutoff** (0–100% in the status line; internally mapped to a one-pole-style cutoff in Hz range appropriate for the demo). |

**SW1** and **SW2** are not used in the current `filters.cpp` main loop.

## Saving

**Model**, **oscillator frequency**, and **cutoff** are **saved to QSPI** after a short quiet period, consistent with the other projects.

## Related

- [README](README.md)  
- [Hardware](hardware.md)
