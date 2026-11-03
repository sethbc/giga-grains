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

/// The patterns a chooser for a sample offers (the loader strip's and the lane menu's).
inline constexpr const char *kSampleFileChooserPatterns = "*.wav;*.aif;*.aiff;*.aifc;*.flac;*.mp3";

/// What both drop targets say of a file that isn't one.
inline juce::String notSampleFileNotice(const juce::String &path)
{
    // The name after either separator: juce::File splits on the native one alone, so on Windows a
    // path with "/" would keep its folders in the message. Trailing separators go first, so a
    // folder dropped as "dir/" is named "dir", not "" (row 388 (q)).
    juce::String trimmed = path;
    while (trimmed.length() > 1 && (trimmed.endsWithChar('/') || trimmed.endsWithChar('\\')))
    {
        trimmed = trimmed.dropLastCharacters(1);
    }
    return "can't load " + trimmed.substring(trimmed.lastIndexOfAnyOf("/\\") + 1) +
           ": not a WAV, AIFF, FLAC or MP3 file";
}
} // namespace gg::ui

#endif // GG_UI_SAMPLEFILES_H
