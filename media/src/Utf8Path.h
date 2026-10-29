// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_SRC_UTF8PATH_H
#define GG_MEDIA_SRC_UTF8PATH_H

// The shared UTF-8 path helpers (gg/media/Utf8Path.h) under the detail:: names Search.cpp still
// calls them by. Remove it once Search.cpp calls gg::media's directly.

#include "gg/media/Utf8Path.h"

namespace gg::media::detail
{
using gg::media::displayName;
using gg::media::fromUtf8;
using gg::media::toGenericUtf8;
using gg::media::toUtf8;
} // namespace gg::media::detail

#endif // GG_MEDIA_SRC_UTF8PATH_H
