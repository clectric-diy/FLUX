# Spark Firmware: Max/MSP Runtime

This folder will contain Max/MSP externals and related files for the Spark module.

## Purpose

- Integration and tools for using Spark with Max/MSP.
- Export Max patches (especially Gen~ code) to Daisy hardware using Oopsy.
- Develop DSP code in Max/Gen~ and deploy to Spark via Daisy Seed.
- PlugData also supports Max-like patching and can export to Daisy via Oopsy.

## About Oopsy

Oopsy is a toolchain that converts Max (Gen~) and Pure Data/PlugData patches into firmware for Daisy Seed-based devices like Spark. Oopsy is included with Max 8.2 and later (available via the Max Package Manager).

- [Oopsy for Daisy (GitHub)](https://github.com/electro-smith/oopsy)
- [Oopsy Documentation (Max Manual)](https://docs.cycling74.com/max8/vignettes/oopsy_overview)
- [Max Package Manager](https://cycling74.com/downloads/)

## About Gen~

Gen~ is Max's low-level DSP environment. Patches created in Gen~ can be exported to Daisy hardware using Oopsy, enabling high-performance audio on Spark.

- [Gen~ Documentation](https://docs.cycling74.com/max8/vignettes/gen_overview)

## About PlugData

PlugData is a modern, cross-platform fork of Pure Data with a Max-like interface and VST/AU support. It supports exporting patches to Daisy via Oopsy.

- [PlugData](https://plugdata.org/)

## Documentation

- [Daisy Audio Software Overview](https://daisy.audio/software/)
- [Max/MSP](https://cycling74.com/products/max)
- [Oopsy for Daisy](https://github.com/electro-smith/oopsy)
- [PlugData](https://plugdata.org/)

See the main [spark/README.md](../README.md) for more details.