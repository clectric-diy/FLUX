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
├── core/          # Shared hardware + matrix logic
├── router/        # Manual 8x8 matrix routing
├── lunetta/       # Generative logic/rhythm variants
└── sequencer/     # Step-sequenced routing variant
```

## Variants

- [Router](router/README.md) — direct, hands-on matrix routing
- [Lunetta](lunetta/README.md) — generative and algorithmic behavior
- [Sequencer](sequencer/README.md) — timed routing patterns

## Notes

- The shared implementation is in `core/` and reused by all variants.
- If you are new to Nexus, start with `router/`.

Community: [clectric.diy](https://clectric.diy)
