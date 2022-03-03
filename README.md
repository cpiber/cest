# cest

Inheritance in C.

This program, similarly to the standard preprocessor, can take a file and produce valid C syntax, providing inheritance features.


## Getting started

1. Clone this repo
1. Compile and view help:
```console
$ make
$ ./cest -h
```

See [examples](./examples) for more info.


## Syntax

Currently only a single parent is supported. Support for multiple parents is planned.

Declare `struct child` to inherit from `struct Base`:
```c
struct Child (struct Base) {
  // child members
};
```

Currently using any struct present in the current file and its includes is possible, as well as names from direct `typedef`s (i.e. `typedef struct ... name;`). Support for separate `typedef`s is planned.

The special name `CEST_MACROS_HERE` is used to denote the place where the casting-macros should be placed.


## Integrating into the build

Since this tool is not part of the regular C-toolchain, it needs to be called separately. Using a build tool like [GNU Make](https://www.gnu.org/software/make/) or [CMake](https://cmake.org/), the following approach can be used:
1. Instruct build tool to transform `.h.in` files and place them in build folder as `.h`
1. Setup up build folder for includes, or directly include from there
