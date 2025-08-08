# FLUX

**FLUX** are firmware programs written for the clectric (or compatible) hardware modules. FLUX define how the module behaves as an oscillator, filter, sequencer, texture generator, or something brand new.

Each FLUX is a self-contained firmware project with:
- A unique behavior or purpose
- Documentation, controls, and patch examples

## FLUX Header Comment
Including a good header comment such as the following example will help everyone to understand your FLUX.

> Hello World Flux
> Author: Charles H. Leggett (chleggett)
> Initial Release: 2025-08-03
> Repository: https://github.com/clectric-diy/FLUX
>  
> Platform: Electrosmith Daisy Seed
> Runtime: C++
> Supported hardware:
>  - clectric Spark-AE
>  - Electrosmith Pod
> 
> Description:
>  This firmware demonstrates a basic example, written in C++, for the
>  supported hardware (listed above), it is intended to be an easy to
>  understand introduction to the Daisy Seed's capabilities.
> 
> License:
>  This code is released under the MIT License.
>  See the LICENSE file in the FLUX repository for details. 

## FLUX Naming Convention
Please create a folder for each FLUX that is prefixed with "FLUX-". Then use PascalCase to give your FLUX a descriptive name. Finally add "-extension" (from the list below) to identify the language used in the FLUX:
- cpp
- maxpat
- ino
- pd
- rs

Feel free to use your own preferred naming convention within the folder.

### Result
So, a Hello World FLUX written in Pure Data (Pd) would be named:
> FLUX-HelloWorld-pd/helloWorld.pd

### Initialization
All [celectric Spark](https://github.com/clectric-diy/Spark-AE) modules will ship with the InitialSpark FLUX:
> FLUX-InitialSpark-pd/initialSpark.pd

## License
Every FLUX committed to this repository must be open sourced via the [MIT Open Source License](https://tlo.mit.edu/understand-ip/exploring-mit-open-source-license-comprehensive-guide).
