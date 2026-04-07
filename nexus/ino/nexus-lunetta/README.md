# Nexus Lunetta: Generative Chaos Engine

**Living, breathing patches.** Lunetta transforms your Nexus into a generative powerhouse, where algorithms continuously evolve your signal routing. Seven distinct modes create everything from subtle mutations to wild, unpredictable transformations—turning static patches into dynamic soundscapes.

## Generative Algorithms

### XOR-RHYTHM
Chaotic mutations that evolve unpredictably. Perfect for ambient textures and constantly changing soundscapes.

### AND-RHYTHM
Logical, geometric patterns with mathematical precision. Creates structured sequences that feel both organic and algorithmic.

### NAND-RHYTHM
Inverted logic for complementary patterns. Layer with AND mode to create complex interference and harmonic relationships.

### SHIFT-RHYTHM
Hypnotic, rotating bit patterns. Smooth, continuous transformations that pulse and evolve with rhythmic precision.

### LFSR
Pseudo-random sequences using shift register math. Deterministic chaos that feels random but maintains musical coherence.

### RUNGLER
Inspired by classic eurorack modules. Creates complex, interwoven patterns that surprise and delight.

### OFF
Pure router mode for manual control. Switch back anytime to regain direct control.

## Creative Power

Lunetta doesn't just route signals—it evolves them. Set a tempo, choose an algorithm, and watch your patches transform themselves. What starts as a simple oscillator-to-filter connection becomes a living system of morphing connections.

Combine with external modules for even more complexity: clock dividers create polyrhythms, LFOs modulate the tempo, sequencers control the mode selection. Lunetta turns Nexus into a generative partner that co-creates with you.

## Why Lunetta?

For when you want your patches to surprise you. For ambient compositions that evolve over hours. For rhythmic complexity that would be impossible to program manually. Lunetta brings algorithmic thinking to modular synthesis—mathematical precision meets musical intuition.

## Build Notes

- Easiest option: from `nexus/ino/`, run `./build.sh lunetta`.
- Arduino IDE option: open `nexus-lunetta.ino`.
- If the IDE can't find `nexus-core`, copy `../libraries/nexus-core/` into your Arduino libraries folder.
