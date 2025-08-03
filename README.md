# Flux

**Flux** are firmware programs written for the clectric (or compatible) hardware modules. Flux define how the module behaves as an oscillator, filter, sequencer, texture generator, or something brand new.

Each Flux is a self-contained firmware project with:
- A unique behavior or purpose
- Documentation, controls, and patch examples

## Flux Naming

### Convention
Please create a folder for each flux that is prefixed with "FLUX-". Then use PascalCase to give your flux a descriptive name. Finally add "-extension" (from the list below) to identify the language used in the flux:
- cpp
- maxpat
- ino
- pd
- rs

Feel free to use your own preferred naming convention within the folder.

### Result
So, our first couple of flux will be named:
- FLUX-HelloWorld-cpp/helloWorld.cpp
- FLUX-HelloWorld-pd/helloWorld.pd

## License
Every Flux committed to this repository must be open sourced via the [MIT Open Source License](https://tlo.mit.edu/understand-ip/exploring-mit-open-source-license-comprehensive-guide).
