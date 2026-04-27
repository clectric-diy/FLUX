# Spark module documentation

User-oriented notes for the **Spark** module firmware in this repository. Spark is built around a [Daisy Seed](https://electro-smith.com/products/daisy-seed)-class core with a custom front panel: two potentiometers, an encoder with push, two momentary switches, and RGB feedback.

## Guides

| Document | What it covers |
| -------- | ---------------- |
| [Hardware](hardware.md) | Knobs, encoder, switches, LEDs (what you see and touch) |
| [Oscillators](oscillators.md) | Main voice firmware: wave and macro banks, modifier layer, saving |
| [Effects](effects.md) | Effects scaffold: model select, test oscillator, mix |
| [Filters](filters.md) | Filters scaffold: model select, test oscillator, cutoff |

## Firmware builds

Source lives under `spark/cpp/`:

- **`oscillators/`** — primary synthesizer / sound engine
- **`effects/`** — effect processing example (oscillator into FX)
- **`filters/`** — filter example (oscillator through multimode filter)

Each folder has its own `Makefile` and produces a loadable image (`.elf` / `.bin` / `.hex`).

## Flashing

- **ST-Link** (SWD): from the firmware directory, `make program` (OpenOCD + ST-Link).
- **USB DFU** (bootloader): `make program-dfu` with the device in DFU mode (typical Electrosmith/STM DFU `0483:df11`).

How you program depends on your dev setup; both paths are valid when the tool matches the interface.

## Settings memory

When firmware calls the shared `SparkRuntime` + `PersistentStorage` pattern, the active patch (mode, model, frequency, and other stored parameters) is written to **QSPI** after a few seconds of inactivity, so the module can recall the last session on power-up.

## Serial console (optional)

With debug logging enabled, status and control events may print over **USB serial** (common baud **115200**). Exact verbosity depends on compile-time debug settings in each project.
