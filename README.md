# Discord

This is a library which implements the formal and informal specifications
provided by Discord for interfacing programs with the Discord infrastructure.

## Usage

The `Discord::Gateway` class is used to interact with Discord gateways.

## Supported platforms / recommended toolchains

This is a portable C++11 library which depends on the C++11 compiler, standard
library, and a few custom C++ libraries with similar dependencies.  It should
be supported on almost any platform.  The following are recommended toolchains
for popular platforms.

The unit tests which accompany the library depend on the library and its
dependencies, as well as the [Google
Test](https://github.com/google/googletest.git) library.

* Windows -- [Visual Studio](https://www.visualstudio.com/) (Microsoft Visual
  C++)
* Linux -- clang or gcc
* MacOS -- Xcode (clang)

## Building

This library is not intended to stand alone.  It is intended to be included in
a larger solution which uses [CMake](https://cmake.org/) to generate the build
system and build applications which will link with the library.

There are two distinct steps in the build process:

1. Generation of the build system, using CMake
2. Compiling, linking, etc., using CMake-compatible toolchain

### Prerequisites

* [CMake](https://cmake.org/) version 3.8 or newer
* C++11 toolchain compatible with CMake for your development platform (e.g.
  [Visual Studio](https://www.visualstudio.com/) on Windows)
* [Json](https://github.com/rhymu8354/Json.git) - a library which implements
  [RFC 7159](https://tools.ietf.org/html/rfc7159), "The JavaScript Object
  Notation (JSON) Data Interchange Format".
* [StringExtensions](https://github.com/rhymu8354/StringExtensions.git) - a
  library containing C++ string-oriented libraries, many of which ought to be
  in the standard library, but aren't.
* [Timekeeping](https://github.com/rhymu8354/Timekeeping.git) - a library
  of classes and interfaces dealing with tracking time and scheduling work

### Build system generation

Generate the build system using [CMake](https://cmake.org/) from the solution
root.  For example:

```bash
mkdir build
cd build
cmake -G "Visual Studio 15 2017" -A "x64" ..
```

### Compiling, linking, et cetera

Either use [CMake](https://cmake.org/) or your toolchain's IDE to build.
For [CMake](https://cmake.org/):

```bash
cd build
cmake --build . --config Release
```
