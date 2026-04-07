# Nexus Modeling in Pure Data (Pd)

This folder is for Pure Data patches that model the behavior of **Nexus** before (or alongside) hardware deployment.

The goal is to create a software-first playground for the Nexus 8×8 routing workflow: matrix switching, preset storage/recall, audition behavior, and variant logic experiments.

## Why model Nexus in Pd

- Prototype routing ideas quickly without reflashing firmware.
- Validate UI and interaction concepts before coding on embedded targets.
- Explore generative and sequenced matrix behavior in a visual patching environment.
- Build learning examples for the clectric.diy community.

## Notes

- Keep naming and behavior aligned with the Nexus firmware concepts in [nexus-core/nexus-core.h](../ino/nexus-core).
- Treat these patches as reference models and creative prototyping tools.

## References

- [Pure Data](https://puredata.info/)
- [PlugData](https://plugdata.org/)
- [Nexus Runtime Overview](../../nexus/README.md)
- [clectric.diy](https://clectric.diy)