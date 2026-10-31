// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_SAMPLEFILES_H
#define GG_UI_SAMPLEFILES_H

#include <juce_core/juce_core.h>

namespace gg::ui
{
/// Whether the path names a file the loader can decode, by its extension: both drop targets (the
/// loader strip and the canvas's lanes) ask this before handing a file over.
inline bool isSampleFilePath(const juce::String &path)
{
    // Not parsed as a juce::File, which needs an absolute path: the loader takes it as given.
    return juce::File::createFileWithoutCheckingPath(path).hasFileExtension(
        "wav;aif;aiff;aifc;flac;mp3");
}

/// What both drop targets say of a file that isn't one.
inline juce::String notSampleFileNotice(const juce::String &path)
{
    return "can't load " + juce::File::createFileWithoutCheckingPath(path).getFileName() +
           ": not a WAV, AIFF, FLAC or MP3 file";
}
} // namespace gg::ui

#endif // GG_UI_SAMPLEFILES_H
