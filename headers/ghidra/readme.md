# Generated ghidra files

## Setup
Use the [ghidra-tooling](https://github.com/jaenster/ghidra-tooling/) and set this up first

## What happens
The ghidra-tooling does several things.
1) Reads `c:\user\username\game.exe.h` (what you config)
    - Parse all `Structs`, `Union`, `Typedefs`, `Enums`
    - It selects which structs/enums are wanted for Charon (by config).
    - It enrich the list by recursively search trough the current set to fetch all dependencies
2) Reads `framework\ghidra.extensions.cpp`
    - Parse all methods for existing D2Structs that are exported out of ghidra and store it in the collection
3) Reads `headers\ghidra\user.extensions.h`
    - Parse all newly created `user defined` classes that are extended from a D2Structs and store it in the collection
    - Example; It contains the class D2UnitItem, which extends the actual struct D2UnitStrc
4) Generates `headers\ghidra\naked.h`
    - Put all blank struct signatures in a file
    - Insert all blank structs from `user.extensions.h`
5) Generates `headers\ghidra\enums.h`
    - Put all needed enums in this file (those that are required and those that are dependencies)
6) Generates `headers\ghidra\main.h`
    - Includes enums
    - Includes naked
    - Lists all `Structs`, `Union`, `Typedefs` in a way its dependencies are above what it requires
    - Add user defined methods to `Structs`
    - Add user defined comments to `Structs`
    
## What to avoid
1) For now, please dont use templated user defined methods/classes/structs
2) Im not sure if unions work properly, please dont

## How to use
In a feature, like `ExperienceMod` you can simple use `#include "headers/ghidra.h"` as before.

## Why a separated headers/ghidra.h?
Because `headers\ghidra\user.extensions.h` need to include `headers\ghidra\main.h`, i cant include it the other way around as well.

For features that require `ghidra.h`, please use the old (and now wrapper) file `headers/ghidra.h` to both have the user defined methods / classes and actual D2Structs
