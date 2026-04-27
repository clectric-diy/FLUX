# Spark hardware (front panel)

This is a user-level map of the **Spark** module controls as exposed by the `Spark` class in `spark/cpp/daisy_spark.h`. Your panel labeling may use different names; this doc uses the same identifiers as the firmware.

## Knobs

| Id | API | Role (typical) |
| -- | --- | -------------- |
| **K1** | `knob1` | Primary continuous control. Meaning depends on firmware (e.g. pitch, test tone frequency). |
| **K2** | `knob2` | Secondary continuous control (e.g. timbre, mix, or cutoff). |

Knobs are read as analog values and, in full builds, the ADC is started at boot so parameters track smoothly.

## Encoder

A rotary encoder with **press** (click). Firmware uses the built-in `Encoder` from libDaisy.

- **Turn** without modifier: often changes the *current program* within the active bank (wave, macro, effect type, etc.).
- **Press** and turn (or **hold SW2** while turning, when the “encoder click workaround” is enabled): can switch *banks* or *top-level modes* (see [Oscillators](oscillators.md#encoder-bank--program-select)).

## Switches

| Id | API | Role in oscillators firmware |
| -- | --- | --------------------------- |
| **SW1** | `button1` | **Octave up** (on release, within allowed range). |
| **SW2** | `button2` | **Short press:** octave down. **Long hold (modifier):** alternate mapping for K1/K2 (see oscillators doc). Used with encoder for bank select when click is not used. |

In **effects** and **filters** scaffolds, the switches are not given special behavior in the current code paths; the encoder and knobs carry most of the interaction.

## LEDs

- **Two onboard RGB LEDs** (`led1`, `led2`): used for *mode* vs *current program* color feedback in the oscillators firmware.
- **Optional I2C RGB** in front of the PEL12T-style encoder: if the build defines an I2C address, the same program color is mirrored on the encoder LED ring for visibility from a normal playing distance.

Calibration (gain and gamma) can be tuned in firmware for a given LED batch.

## Audio and I/O

The Spark class wraps a Daisy Seed–compatible audio path. Exact jack assignment is determined by the BSP and module routing; see the `Spark::Init` implementation and the specific firmware’s `AudioCallback` for input/output usage.

## MIDI (developer note)

A `MidiUartHandler` is present on the `Spark` object. Whether MIDI is used depends on the firmware; check the `main` and audio loop of each project.
