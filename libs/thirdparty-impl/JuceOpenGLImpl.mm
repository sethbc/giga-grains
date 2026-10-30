// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The GPU canvas spike (M2 sub-project 6, Task 17; spike/gpu-canvas only): juce_opengl compiled
// here and nowhere else (target gg::juce-opengl-impl, libs/CMakeLists.txt), as the other JUCE
// modules are compiled once in clap_juce_shim. JUCE's module .cpp must be the only include of
// its translation unit. On macOS the module's .mm (Objective-C++) wraps the .cpp.

#include <juce_opengl/juce_opengl.mm>
