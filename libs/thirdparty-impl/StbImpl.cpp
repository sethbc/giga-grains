// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The stb_image_write implementation, compiled here and nowhere else (target gg::stb_impl,
// libs/CMakeLists.txt). Our sources include the header without the define.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
