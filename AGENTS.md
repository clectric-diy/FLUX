# AGENTS.md

## Cursor Cloud specific instructions

This is an embedded firmware monorepo (FLUX) for the clectric.diy synth DIY community targeting the AE Modular format. There are no web servers, databases, or runtime services — only cross-compilation toolchains for bare-metal microcontrollers.

### Repo structure (two independent hardware modules)

| Module | Platform | Toolchain | Build entry point |
|--------|----------|-----------|-------------------|
| **Spark** | Daisy Seed (STM32H750 / Cortex-M7) | `arm-none-eabi-gcc` + Make | `spark/cpp/ci/build_libs.sh` then `make` in each project dir |
| **Nexus** | ATmega4809-A (Arduino) | `arduino-cli` + `arduino:megaavr` core | `nexus/ino/build.sh <variant>` |

### Building Spark firmware

1. Submodules must be initialized: `git submodule update --init --recursive`
2. Build libraries first: `bash spark/cpp/ci/build_libs.sh`
3. Build individual projects: `make -C spark/cpp/filters`, `make -C spark/cpp/effects`, etc.
4. The `oscillators` project may overflow FLASH with certain toolchain versions — this is a pre-existing code size issue, not an environment problem.

### Building Nexus firmware

- `cd nexus/ino && bash build.sh all` builds all three variants (router, lunetta, sequencer).
- Variants can be built individually: `bash build.sh router`, `bash build.sh lunetta`, `bash build.sh sequencer`.
- Required Arduino libraries (U8g2, Adafruit GFX Library, Adafruit SSD1306) must be installed via `arduino-cli lib install`.

### Coding guidelines

See `.github/copilot-instructions.md` for comprehensive coding standards, naming conventions, and philosophy. Key points: beginner-first code style, Nexus-specific terminology canon, and modular design patterns.

### No lint/test framework

This repo has no formal linter or test runner. "Testing" means successful cross-compilation for the target platforms. The build scripts serve as the primary verification mechanism.
