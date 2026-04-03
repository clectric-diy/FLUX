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
  - **nexus/ino/core/**: Shared Nexus hardware/matrix implementation (`nexus-core.h/.cpp`)
  - **nexus/ino/router/**: Pure signal routing variant
  - **nexus/ino/lunetta/**: Generative chaos engine variant
  - **nexus/ino/sequencer/**: Step pattern sequencer variant
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
- **Firmware**: Called FLUX are licensed under the MIT License
- **Hardware**: Are licensed under the CERN Open Hardware License (OHL) v2 or later. Some are strongly reciprocal others are weakly reciprocal. See individual repos for details.
- **Documentation/Libraries**: are licensed under the Creative Commons Attribution Share Alike 4.0 International License (CC BY-SA 4.0).
- Always check the repo or [licensing page](https://clectric.diy/licensing) for details

### 4. Naming & Branding
- **clectric.diy**: Lowercase, always with the dot; refers to the community and online makerspace
- **FLUX**: All caps for the firmware platform/brand
- **Nexus, Spark, Arc, Shock**: Capitalized for module names
- **Directory names**: Lowercase for URLs and file paths (e.g., `nexus/ino/lunetta/`)

### 5. Development Guidelines
- **Modular Design**: Shared code in core libraries, variant-specific in subfolders
- **Documentation**: Focus on creative/functional descriptions, not just technical details
- **Testing**: Each module/variant should compile and run independently
- **Hardware Compatibility**: AE Modular standard (0-5V), Daisy Seed, ATmega4809-A, I2C/SPI expansion for Arc modules
- **Post-refactor path convention**: Spark library/tooling paths now live under `spark/cpp/` (not repo root). Prefer script-relative path resolution in tooling and CI.
- **Nexus runtime convention**: Arduino projects live under `nexus/ino/`; keep shared implementation in `nexus/ino/core/` and variants in sibling folders.

### 6. Post-Refactor Guardrails (Current State)
- **Do not reintroduce old root paths**: `libDaisy/`, `DaisySP/`, `stmlib/`, `helper.py`, and `ci/` are under `spark/cpp/`.
- **Spark build/tooling references** should target `spark/cpp/...` and remain script-relative where possible.
- **Nexus shared includes** in variants should use `../core/nexus-core.h` from `nexus/ino/{router,lunetta,sequencer}/`.
- **Runtime discoverability matters**: prefer explicit runtime folders like `nexus/ino/` and `spark/{cpp,ino,max,pd,rs}/` for beginner clarity.
- **Brand/module style**: use `Nexus` (not all-caps) in user-facing docs.

## Quick References
- [clectric.diy](https://clectric.diy) — Main site, project overviews, and resources
- [clectric-diy GitHub](https://github.com/clectric-diy) — All repositories
- [FLUX repo](https://github.com/clectric-diy/FLUX) — This firmware repository
- [Nexus-AE repo](https://github.com/clectric-diy/Nexus-AE) — Hardware and generative sequencer
- [Spark-AE repo](https://github.com/clectric-diy/Spark-AE) — Daisy Seed-based module
- [Licensing](https://clectric.diy/licensing) — License details for all projects
- [Discussions](https://github.com/orgs/clectric-diy/discussions) — Community forum

## Example Directory Structure
```
FLUX/
├── nexus/               # Arduino-based AE Modular switch matrix
│   └── ino/             # Arduino runtime projects
│       ├── core/        # Shared hardware + matrix implementation
│       ├── router/      # Pure signal routing
│       ├── lunetta/     # Generative chaos engine
│       └── sequencer/   # Step pattern sequencer
├── spark/               # Daisy Seed-based module(s)
│   └── cpp/             # C++ firmware and build dependencies
│       ├── libDaisy/    # Daisy Seed HAL (git submodule)
│       ├── DaisySP/     # Daisy DSP library (git submodule)
│       ├── stmlib/      # STM32 utilities (git submodule)
│       ├── daisy_spark.h    # Spark Board Support Package (BSP) header
│       ├── daisy_spark.cpp  # Spark BSP implementation
│       ├── helper.py    # Project management utility (create/copy/update projects)
│       ├── ci/          # CI scripts (build_libs.sh)
│       └── spark-init/  # SynthVoice-based firmware, expanding to effects/looper
│   ├── ino/             # Arduino runtime projects
│   ├── max/             # Max/MSP runtime projects
│   ├── pd/              # Pure Data runtime projects
│   └── rs/              # Rust runtime projects
└── arcs/                # Expansion modules (fader, display, etc.) for Spark/Nexus
```

## Project Philosophy
- **Open source first**: All code, hardware, and docs are open for remixing and learning
- **Community-driven**: Designed for and by the Synth DIY community
- **Creative empowerment**: Lower barriers, encourage experimentation, and support new makers
- **Branding**: FLUX is the firmware platform; clectric.diy is the community and resource hub; clectric is the overarching entity (LLC)
- **Naming**: To prevent confusion, makers are encouraged to refer to their own builds by the module name (e.g., "Nexus" or "Spark") and refer to the using FLUX firmware, but must avoid using "clectric" or "electric" in their own project names to prevent confusion with the clectric brand.

## See Also
- [store.clectric.diy](https://store.clectric.diy/) — Hardware kits and modules
- [AE Modular format](https://clectric.diy/formats/ae-modular/)
- [KiCad tools](https://clectric.diy/tools/KiCad.html)
- [Guidebook](https://github.com/clectric-diy/clectricDIY-Guidebook)