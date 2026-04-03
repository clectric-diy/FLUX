# Spark Firmware: C++ Runtime

This folder contains the C++ firmware and tools for the Spark module, built on the Daisy Seed platform. It's the recommended path for real-time DSP and getting the most out of your hardware.

## Directory Structure

```
cpp/
├── daisy_spark.h     # Spark Board Support Package (BSP) — pin map and hardware init
├── daisy_spark.cpp   # Spark BSP implementation
├── spark-init/       # The FLUX shipped on clectric Spark modules
├── libDaisy/         # Daisy Seed HAL and drivers (git submodule)
├── DaisySP/          # DSP building blocks — oscillators, filters, effects (git submodule)
├── stmlib/           # STM32 utility library (git submodule)
├── helper.py         # Project management utility (create, copy, update projects)
├── ci/build_libs.sh  # Build libDaisy and DaisySP static libraries

```

The `libDaisy`, `DaisySP`, and `stmlib` libraries live here as git submodules — keep them up to date with:

```
git submodule update --init --recursive
```

## Flashing Your Module

The easiest way to load firmware onto your Spark is with the [Daisy Web Flasher](https://flash.daisy.audio) — just connect via USB and drop in your `.bin` file, no software install required.

## Main Libraries & Tools

- [libDaisy](https://github.com/electro-smith/libDaisy) — Hardware abstraction and drivers for Daisy Seed
- [DaisySP](https://github.com/electro-smith/DaisySP) — DSP building blocks for audio synthesis and effects
- [DaisyExamples](https://github.com/electro-smith/DaisyExamples) — Reference projects and templates
- [Daisy Wiki](https://github.com/electro-smith/DaisyWiki/wiki) — Guides, tips, and hardware info

## Documentation

- [libDaisy API Docs](https://electro-smith.github.io/libDaisy/)
- [DaisySP API Docs](https://electro-smith.github.io/DaisySP/)
- [Daisy Audio Software Overview](https://daisy.audio/software/)

See the main [spark/README.md](../README.md) for other Spark runtimes (Arduino, Max, Pure Data, Rust).