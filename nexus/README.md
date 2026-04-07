# Nexus Firmware (Arduino Runtime)

Nexus is an **8×8 analog switch matrix** for AE Modular—powered by the **ATmega4809-A** and **ADG2188**—that turns patching into an instrument.

Instead of repatching cables every time you want a new idea, Nexus lets you store, recall, and evolve routing states in firmware. It can be a clean utility one moment and a wild composition tool the next.

## Why Nexus is cool

- **64 possible crosspoints** in one compact module
- **Instant routing changes** for performance and live experimentation
- **Preset recall** to jump between patch states
- **Multiple personalities**: manual router, generative engine, and sequencer
- **AE-native mindset**: practical utility + creative chaos in one system
- **Real hardware muscle**: ATmega4809-A control + ADG2188 analog crosspoint routing

## Why build or buy one

- You get more mileage out of the modules you already own.
- It unlocks patch ideas that are hard (or impossible) to do with cables alone.
- It grows with you: start with Router, then explore Lunetta and Sequencer.
- It is open-source and maker-friendly, so you can remix behavior to match your own workflow.

This layout is intentionally beginner-friendly so it's easy to find the right files quickly.

## Directory Structure

```
nexus/
├── ino/
│   ├── libraries/
│   │   └── nexus-core/   # Bundled Arduino library with shared Nexus runtime
│   ├── nexus-router/     # Manual 8x8 matrix routing
│   ├── nexus-lunetta/    # Generative logic/rhythm variants
│   └── nexus-sequencer/  # Step-sequenced routing variant
└── pd/                   # Pure Data exploration and supporting assets
```

## Variants

- [Router](ino/nexus-router/README.md) — direct, hands-on matrix routing
- [Lunetta](ino/nexus-lunetta/README.md) — generative and algorithmic behavior
- [Sequencer](ino/nexus-sequencer/README.md) — timed routing patterns

## Notes

- If you are new to Nexus, start with `ino/nexus-router/`.
- If you prefer the Arduino IDE and it can't find `nexus-core`, copy `ino/libraries/nexus-core/` into your Arduino libraries folder.

## Quick Build

- Go to `nexus/ino/` and run `./build.sh router`.
- Use `./build.sh lunetta`, `./build.sh sequencer`, or `./build.sh all` as needed.
- Optional board override: `FQBN=... ./build.sh router`.

Community: [clectric.diy](https://clectric.diy)
