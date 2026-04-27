# Oscillators firmware — user guide

The **oscillators** build is the main **Spark** sound engine. It offers three *banks* (top-level modes), two knobs, octave switches, an encoder, and a **modifier** layer on **SW2** that remaps the knobs for extra parameters without losing your base pitch and timbre.

## Banks (modes)

Turn the **encoder** to step through the program within the current bank. Bank colors and “which LED is which” are chosen so you can read **mode** vs **program** at a glance (see *LEDs* below).

1. **Wave** — classic oscillators: sine through supersaw.
2. **Macro A** — six extended algorithms (variable saw, variable shape, FM, formant, harmonics, Zosc).
3. **Macro B** — six more voices (Vosim, string, particle, grain-style texture, overdrive, bass/kick style).

## Encoder: bank + program select

- **Turn** the encoder: cycles the **active model** in the current bank (which waveform or macro).
- **Hold SW2** and **turn** the encoder (or **press the encoder and turn** when the click is working): cycles **which bank** you are in (Wave, Macro A, Macro B).  
  - When a dedicated encoder click is unreliable, the firmware is often built with a **“hold SW2”** style bank change — watch the serial banner or use the behavior above.

## Knobs: K1 and K2 (normal)

| Control | Name in logs | Function |
| ------- | ------------ | -------- |
| **K1** | “k1” | **Pitch**: continuous frequency around **middle C**, with **±1 octave** of fine range from the knob, plus the octave switch shifts (below). The mapping is **smooth in software** so small electrical noise on the pot does not drift the note when you are not moving it. |
| **K2** | “k2” | **Timbre** (or the closest “tone” control for the current model). Slightly **smoothed** in software. |

**Octave switches**

- **SW1** (short press / release): **octave up** (up to **+2** octaves from the base range).
- **SW2** **short** press: **octave down** (down to **-2** octaves).

The serial-friendly line can show the resulting frequency in hertz and approximate timbre as a percentage.

## Modifier: hold SW2 (k3 and k4)

**Hold SW2** past the “hold” threshold (a few hundred milliseconds) to enter **modifier** mode. While held:

| Physical control | In modifier mode | In USB logs (example) |
| ---------------- | ---------------- | ----------------------- |
| **K1** | **Harmonics** (or harmonic-related amount for the current model) | `k3` / harmonics % |
| **K2** | **Morph** (secondary, model-dependent) | `k4` / morph % |

- **K1 and K2 no longer re-tune the stored pitch and timbre** in the way they do in normal play; the modifier parameters are **separate** and updated while you turn.
- When you **release SW2**, pitch and timbre **do not jump** to wherever the knobs happen to be after editing harmonics/morph. The engine **latches** the last pitch and timbre until you **move K1** or **K2** by a clear amount to “wake” that parameter again. That way k3 and k1 do not feel glued together when you let go of the switch.

(Exact thresholds and smoothing are in `oscillators.cpp`; the intent is: predictable musical behavior, not a sudden retune on release.)

## Model list (current names)

**Wave bank:** Sine, Tri, Saw, Square, Ramp, SuperSaw.

**Macro A:** VarSaw, VarShape, FM2, Formant, Harmonic, ZOsc.

**Macro B:** Vosim, String, Particle, Grainlet (UI may still call this slot “ring mod / noise” in some builds), Drive, Kick.

**Encoder click without turning** in Macro B: **String** and **Kick** can **retrigger** a note or hit on release, depending on the current model (used as a playability shortcut).

## LEDs

- **LED 1 (dimmer)**: color indicates **which bank** (Wave / Macro A / Macro B).
- **Brighter / LED 2 / encoder ring**: color indicates **which program** within the bank, using a fixed **ROYGBIV-style** palette (red is intentionally last in the order).

## Saving

After you stop tweaking for a few seconds, settings are **saved to flash (QSPI)** and come back on the next power cycle (mode, waveform/macro, frequency, and related stored state).

## USB serial (optional)

With debug output enabled, you can connect at **115200** baud to see one-line **status** and **control** messages (mode changes, knob summaries, save notifications). Handy for development; not required to play the module from the panel.

If an **I2C scan** runs on boot, it is for developer bring-up and peripheral detection and does not affect normal playing.

## Related

- [Hardware](hardware.md) — physical control map  
- [README](README.md) — other firmware and flashing
