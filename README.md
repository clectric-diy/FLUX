# FLUX

**FLUX** are firmware programs written for the clectric (or compatible) hardware modules. FLUX define how the module behaves as an oscillator, filter, sequencer, texture generator, or something brand new.

Each FLUX is a self-contained firmware project with:
- A unique behavior or purpose
- Documentation, controls, and patch examples

## FLUX Naming

### Convention
Please create a folder for each FLUX that is prefixed with "FLUX-". Then use PascalCase to give your FLUX a descriptive name. Finally add "-extension" (from the list below) to identify the language used in the FLUX:
- cpp
- maxpat
- ino
- pd
- rs

Feel free to use your own preferred naming convention within the folder.

### Result
So, a Hello World FLUX written in pure data would be named:
> FLUX-HelloWorld-pd/helloWorld.pd

### Initialiation
The default FLUX installed on all [celectric Spark](https://github.com/clectric-diy/Spark-AE) modules will be:
> FLUX-InitialSpark-pd/initialSpark.pd

## License
Every FLUX committed to this repository must be open sourced via the [MIT Open Source License](https://tlo.mit.edu/understand-ip/exploring-mit-open-source-license-comprehensive-guide).
