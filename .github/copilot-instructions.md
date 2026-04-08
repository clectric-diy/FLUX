# GitHub Copilot Instructions (FLUX: clectric.diy)

## Project snapshot
- **FLUX** is a multi-module firmware repository for the clectric.diy Synth DIY community, supporting both Arduino and Daisy Seed platforms for the AE Modular format.
- **Branding**: FLUX is part of the clectric.diy ecosystem—an online makerspace for experimentation, learning, and sharing, with a focus on open source and community-driven synth DIY projects.
- **Modules**:
  - **Nexus**: Arduino-based, 8x8 analog switch matrix for AE Modular (ATmega4809-A, ADG2188, SSD1306 OLED)
  - **Spark**: Daisy Seed-based, embedded DSP platform for AE Modular (C++/DaisySP)
  - **Arc modules**: A collection of lightweight expansion modules for Spark, Nexus, and other microcontroller-based modules. Arc modules communicate via I2C and SPI expansion ports and add new controls and features (e.g., Fader Arc for slide potentiometers, Display Arc for TFT screens, etc.).
- **Website**: [clectric.diy](https://clectric.diy) — project overviews, resources, and community links
- **Licensing**: MIT, CERN OHL, and Creative Commons licenses used across projects (see individual repos and [licensing](https://clectric.diy/licensing))
- **Naming conventions**: Lowercase directory names for clean URLs; module names are capitalized (e.g., Nexus, Spark, Arc, FLUX)

## What AI agents should know first

### 1. Repository Structure & Scope
- **nexus/**: Arduino-based firmware
  - **nexus/ino/**: Arduino sketches and build scripts
    - **nexus/ino/libraries/nexus-core/**: Shared Nexus hardware/matrix/runtime library (`nexus-core.h/.cpp`)
    - **nexus/ino/nexus-router/**: Pure signal routing variant
    - **nexus/ino/nexus-lunetta/**: Generative chaos engine variant
    - **nexus/ino/nexus-sequencer/**: Step pattern sequencer variant
    - **nexus/ino/build.sh**: Local compile helper for Nexus variants
    - **nexus/ino/scripts/sync-nexuscore.sh**: Sync helper for IDE upload workflows
  - **nexus/pd/**: Pure Data support files for Nexus
- **spark/**: Spark runtimes and firmware workflows
  - **spark/cpp/**: Main C++ workflow; contains `libDaisy`, `DaisySP`, and `stmlib` submodules, Spark BSP (`daisy_spark.h/.cpp`), `helper.py`, `ci/`, and projects like `spark-init/`
  - **spark/ino/**, **spark/max/**, **spark/pd/**, **spark/rs/**: Additional Spark runtime ecosystems (Arduino, Max, Pure Data, Rust)
- **arcs/**: I2C and SPI digital Expansion modules to provide additional controls and features for Spark, Nexus, and other microcontroller-based modules
- **.github/**: Copilot instructions, funding, and community files

### 2. Community & Philosophy
- **clectric.diy** is a dba of clectric, LLC, with the intention to become a nonprofit
- Focus on open source, low barrier to entry, and creative collaboration
- Projects are designed for learning, remixing, and sharing—see [resources](https://clectric.diy/resources)
- Community discussions at [GitHub Discussions](https://github.com/orgs/clectric-diy/discussions)

### 3. Licensing
- **Firmware**: FLUX firmware is licensed under the MIT License.
- **Hardware**: Hardware designs are licensed under the CERN Open Hardware License (OHL) v2 or later. Some designs are strongly reciprocal and others are weakly reciprocal. See individual repositories for details.
- **Documentation/Libraries**: Documentation and libraries are licensed under the Creative Commons Attribution Share Alike 4.0 International License (CC BY-SA 4.0).
- Always check the repo or [licensing page](https://clectric.diy/licensing) for details

### 4. Naming & Branding
- **clectric.diy**: Lowercase, always with the dot; refers to the community and online makerspace
- **FLUX**: All caps for the firmware platform/brand
- **Nexus, Spark, Arc, Shock**: Capitalized for module names
- **Directory names**: Lowercase for URLs and file paths (e.g., `nexus/lunetta/`)

### 5. Development Guidelines
- **Modular Design**: Shared code in core libraries, variant-specific in subfolders
- **Documentation**: Focus on creative/functional descriptions, not just technical details
- **Testing**: Each module/variant should compile and run independently
- **Hardware Compatibility**: AE Modular standard (0-5V), Daisy Seed, ATmega4809-A, I2C/SPI expansion for Arc modules
- **Spark path convention**: Spark library/tooling paths live under `spark/cpp/` (not repo root). Prefer script-relative path resolution in tooling and CI.
- **Nexus runtime convention**: Arduino projects live under `nexus/ino/`; keep shared implementation in the bundled Arduino library at `nexus/ino/libraries/nexus-core/src/` and keep variants as sibling sketches.

### 5.1 First-Principles Coding Standard (Beginner-First)
- **Primary audience**: New makers and novice programmers learning embedded systems through Spark and Nexus.
- **Textbook style required**: Write code that reads like a teaching example, not a code-golf or clever production trick.
- **Naming clarity over brevity**: Prefer intuitive, descriptive names (`encoderDirection`, `isButtonPressed`) over short/ambiguous names (`dir`, `bp`, `tmp`).
- **Simple and elegant solutions**: Prefer the smallest understandable design that works reliably. Avoid unnecessary abstractions, metaprogramming, or hidden control flow.
- **Single-responsibility functions**: Keep functions focused and short enough that beginners can follow them in one read.
- **Explain intent, not noise**: Comments should explain *why* something exists or *how* hardware behavior maps to code, not restate obvious syntax.
- **Teaching-friendly structure**: Group related constants, clearly separate setup/runtime paths, and keep state transitions explicit.
- **Predictable behavior first**: Avoid “magic” side effects; make state changes and timing assumptions visible and documented.

### 5.2 Documentation & Comment Tone (Community Onboarding)
- **Inviting tone**: Comments and README content should welcome new community members and assume curiosity, not prior expertise.
- **Starter context**: For each module/runtime, include a plain-language overview, what problem it solves, and where to begin editing.
- **Learning path**: Prefer docs that help users take a first step, then a second step (build → flash → tweak one parameter).
- **Glossary mindset**: Define hardware or DSP terms briefly when first introduced.
- **Mentorship expectation**: Intermediate and advanced contributors should model supportive explanations and leave clear breadcrumbs for novices.
- **No gatekeeping language**: Avoid wording that shames beginners or implies only experts should modify code.

### 5.3 Nexus Terminology Canon (Enforced)
- **Use synth-native hierarchy** in Nexus code and comments: **matrix → patch → preset patch → connection**.
- **Patch vocabulary**:
  - Prefer `Patch`, `activePatch`, and `presetPatches`.
  - A patch is a complete routing configuration.
  - A connection is one on/off route bit within that patch.
  - In modular synth terms, think of a patch as the full cable setup for the current sound/behavior; in Nexus this is represented digitally by the full 8x8 routing configuration.
  - Teaching cue for comments/docs: explain patch first as "the full routing setup," then describe individual connections as the smaller parts inside it.
- **Memory vocabulary**:
  - Prefer `Memory` terminology in public constants, function names, and logs (`save...ToMemory`, `load...FromMemory`, `MEMORY_*`).
  - Use `EEPROM` only when calling the Arduino EEPROM API or explicitly discussing that hardware API.
- **Switch vocabulary**:
  - Use role-based names for the three switch chips: `INPUT_SWITCH_*`, `GENERATED_SWITCH_*`, `ROUTING_SWITCH_*`.
  - Use abstraction names in APIs (`RoutingSwitch`, `Switch`) and keep chip-specific term `ADG2188` in comments/logs that describe the actual IC.
- **Write-helper naming**:
  - Prefer generic helper names like `writeToSwitch(...)`.
  - Prefer register-oriented wording for addressed writes: `writeSwitchRegister(...)`.
- **Avoid backsliding to older terms** unless required for compatibility:
  - Avoid legacy public names containing `State`, `EEPROM`, `ADG2188` (as API names), `SOCKET_INPUT_SIGNALS`, or `...Row` where `Register` is the intended abstraction.

### 6. Repository Guardrails
- **Spark root layout**: `libDaisy/`, `DaisySP/`, `stmlib/`, `helper.py`, and `ci/` are located under `spark/cpp/`.
- **Spark build/tooling references** should target `spark/cpp/...` and remain script-relative where possible.
- **Nexus shared includes** in variants should use Arduino library include style (`#include <nexus-core.h>`) from `nexus/ino/nexus-{router,lunetta,sequencer}/`.
- **Nexus shared code location**: keep shared runtime in `nexus/ino/libraries/nexus-core/src/`; do not duplicate variant-specific copies.
- **Runtime discoverability matters**: prefer explicit runtime folders like `nexus/{ino,pd}` and `spark/{cpp,ino,max,pd,rs}` for beginner clarity.
- **Brand/module style**: use `Nexus` (not all-caps) in user-facing docs.

## Project Philosophy
- **Open source first**: All code, hardware, and docs are open for remixing and learning
- **Community-driven**: Designed for and by the Synth DIY community
- **Creative empowerment**: Lower barriers, encourage experimentation, and support new makers
- **Branding**: FLUX is the firmware platform; clectric.diy is the community and resource hub; clectric is the overarching entity (LLC)
- **Naming**: To prevent confusion, makers are encouraged to refer to their own builds by module name (e.g., "Nexus" or "Spark") and describe their software as using FLUX firmware. Avoid using "clectric" or "electric" in project names to prevent brand confusion.

## AI Agent Contribution Rules (Enforced)
- Prefer readability over cleverness in every code change.
- When choosing between two valid implementations, choose the one a beginner can debug with a serial monitor and a datasheet.
- Add or refine comments/README text when behavior is non-obvious, hardware-specific, or likely to confuse first-time contributors.
- Prefer short, plain-language explanations in comments and docs before introducing advanced terms.
- Keep public interfaces and variable names self-explanatory; avoid unexplained acronyms.
- If complexity is required, document the constraint and provide a short “how to reason about this” note.

## See Also (Tools We Use)
- **Tooling preference**: Prefer open-source and free-to-use tools whenever possible.
- [Arduino CLI](https://arduino.github.io/arduino-cli/latest/) — Build/upload automation for Arduino workflows
- [Git](https://git-scm.com/) — Version control and collaboration
- [GitHub](https://github.com/) — Source hosting, issues, and discussions
- [Visual Studio Code](https://code.visualstudio.com/) — Primary code editor for firmware, docs, and integrated build/debug workflows
- [KiCad](https://www.kicad.org/) — Open-source PCB and schematic design
- [FreeCAD](https://www.freecad.org/) — Open-source mechanical/CAD design
- [Pure Data](https://puredata.info/) — Open-source visual programming for audio and interaction
- [PlugData](https://plugdata.org/) — Open-source visual patching environment based on Pure Data, with plugin and standalone workflows
- [Max](https://cycling74.com/products/max) — Optional commercial runtime used in some Spark workflows