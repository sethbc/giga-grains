// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The dr_wav, dr_flac and dr_mp3 implementations, compiled here and nowhere else (target
// gg::dr_libs_impl, libs/CMakeLists.txt). Our sources include the headers without these defines.

#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>
#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>
