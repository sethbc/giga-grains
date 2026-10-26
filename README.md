# Giga Grains

Giga Grains is an open-source (AGPL-3.0-or-later) pair of plugins: *Giga Grains*, a granular
synthesizer, and *Giga Grains FX*, a granular effect, both built from one shared C++20 grain
engine as CLAP, VST3, AU v2 and standalone for macOS, Windows and Linux. Read
[`INTENT.md`](INTENT.md) first: it says what the project is for and how it is built. To build,
fetch the dependencies with `git submodule update --init`, then configure, build and test with
CMake 3.22 or newer and Ninja:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```
