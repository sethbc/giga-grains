// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The stb_image_write implementation, and stb_image's PNG reader (which only the spectrogram
// tests use), compiled here and nowhere else (target gg::stb_impl, libs/CMakeLists.txt). Our
// sources include the headers without the IMPLEMENTATION defines; the reader's STBI_* options
// below must match what they define before including stb_image.h.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
