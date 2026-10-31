// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Search.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <utf8proc.h>

#include "TestHooks.h"
#include "gg/media/SettingsFile.h"
#include "gg/media/Sha256.h"
#include "gg/media/Utf8Path.h"

namespace gg::media
{
namespace fs = std::filesystem;

namespace
{
/// The scan's test hook (TestHooks.h); nullptr unless a test sets one.
std::atomic<detail::ScanHook> scanHook{nullptr};

/// How many candidate files the searches have hashed (TestHooks.h).
std::atomic<unsigned> candidateHashes{0};

/// Unicode's two canonical normal forms: composed (NFC) and decomposed (NFD).
enum class Form
{
    C,
    D,
};

/// UTF-8 `text` in normal form `form`, or nullopt if it isn't valid UTF-8 (or memory runs out).
/// ASCII is its own normal form in both, and is copied as it is.
std::optional<std::string> normalized(std::string_view text, Form form)
{
    if (std::all_of(text.begin(), text.end(),
                    [](char c) { return static_cast<unsigned char>(c) < 0x80; }))
    {
        return std::string(text);
    }
    utf8proc_uint8_t *out = nullptr;
    const utf8proc_ssize_t length =
        utf8proc_map(reinterpret_cast<const utf8proc_uint8_t *>(text.data()),
                     static_cast<utf8proc_ssize_t>(text.size()), &out,
                     UTF8PROC_STABLE | (form == Form::C ? UTF8PROC_COMPOSE : UTF8PROC_DECOMPOSE));
    if (length < 0 || out == nullptr)
    {
        return std::nullopt;
    }
    // Freed even if the string's allocation throws.
    struct Free
    {
        void operator()(utf8proc_uint8_t *p) const noexcept { utf8proc_free(p); }
    };
    const std::unique_ptr<utf8proc_uint8_t, Free> owned(out);
    return std::string(reinterpret_cast<const char *>(out), static_cast<std::size_t>(length));
}

/// The spellings of UTF-8 `text` a search tries, in order and each once: as saved, in NFC, in
/// NFD. A name saved on one filesystem can be stored in the other normal form on another: macOS's
/// HFS+ stores names decomposed, and ext4 and NTFS keep whichever they're given.
std::vector<std::string> spellings(const std::string &text)
{
    std::vector<std::string> out{text};
    for (const Form form : {Form::C, Form::D})
    {
        if (auto other = normalized(text, form);
            other && std::find(out.begin(), out.end(), *other) == out.end())
        {
            out.push_back(std::move(*other));
        }
    }
    return out;
}

/// Whether a listed file name is the saved one, as Unicode text: canonically equivalent names (the
/// same once both are in NFC) match, so a name saved in NFC matches its NFD spelling and the other
/// way round. A name that isn't valid UTF-8 matches only itself, byte for byte.
class NameMatcher
{
  public:
    explicit NameMatcher(const std::string &name) : name_(name), nfc_(normalized(name, Form::C)) {}

    bool matches(const fs::path &filename) const
    {
        const auto text = toUtf8(filename);
        if (!text)
        {
            return false;
        }
        if (*text == name_)
        {
            return true;
        }
        if (!nfc_)
        {
            return false;
        }
        const auto nfc = normalized(*text, Form::C);
        return nfc && *nfc == *nfc_;
    }

  private:
    std::string name_;
    std::optional<std::string> nfc_;
};

/// Opens `folder` for the scan to list, through the test hook when one is set.
fs::directory_iterator openFolder(const fs::path &folder, std::error_code &error)
{
    if (const auto hook = scanHook.load(std::memory_order_relaxed))
    {
        error = hook(detail::ScanStep::Open, folder);
        if (error)
        {
            return {};
        }
    }
    return fs::directory_iterator(folder, error);
}

/// What checking one candidate found.
enum class Check
{
    Skipped, // relative, or already checked
    NoFile,  // no regular file there, or one that can't be read
    Differs, // a regular file of another size or SHA-256
    Matches, // the entry's file
};

/// The candidates one search has checked, and the check itself.
class Candidates
{
  public:
    explicit Candidates(const core::MediaEntry &entry) : entry_(entry) {}

    Check check(const fs::path &candidate)
    {
        if (!candidate.is_absolute())
        {
            return Check::Skipped;
        }
        const fs::path key = candidate.lexically_normal();
        if (std::find(checked_.begin(), checked_.end(), key) != checked_.end())
        {
            return Check::Skipped;
        }
        checked_.push_back(key);

        std::error_code error;
        if (!fs::is_regular_file(candidate, error))
        {
            return Check::NoFile;
        }
        const auto size = fs::file_size(candidate, error);
        if (error)
        {
            return Check::NoFile;
        }
        if (size != entry_.fileSize)
        {
            return Check::Differs;
        }
        // On a file system that ignores normalization the saved, NFC and NFD spellings name one
        // file: reuse the result of a spelling already hashed rather than hash it again.
        for (const auto &prior : hashed_)
        {
            std::error_code same;
            if (fs::equivalent(prior.first, candidate, same) && !same)
            {
                return prior.second;
            }
        }
        candidateHashes.fetch_add(1, std::memory_order_relaxed);
        const auto hash = hashFile(candidate);
        const Check result = !hash ? Check::NoFile
                             : hash->size == entry_.fileSize && hash->sha256Hex == entry_.sha256Hex
                                 ? Check::Matches
                                 : Check::Differs;
        hashed_.emplace_back(candidate, result);
        return result;
    }

  private:
    const core::MediaEntry &entry_;
    std::vector<fs::path> checked_;
    std::vector<std::pair<fs::path, Check>> hashed_; // size matched: the file was hashed
};

/// The index of the saved path's last separator, `/` or `\` (a path saved on Windows), or npos.
std::size_t lastSeparator(const std::string &path) { return path.find_last_of("/\\"); }

/// Step 5c: the first file under `root` whose name `name` matches and whose content matches, or
/// nothing. Walked by hand, depth first (each folder's contents right after its entry), with a
/// listing per open folder: a folder that can't be opened, or fails partway through its listing,
/// is skipped and the scan goes on, where std::filesystem::recursive_directory_iterator would end
/// altogether on any error but EACCES. Visits at most kMaxScanEntries entries and never follows a
/// symlinked folder.
std::optional<fs::path> scan(const fs::path &root, const NameMatcher &name, Candidates &candidates)
{
    std::vector<fs::directory_iterator> open;
    std::error_code error;
    fs::directory_iterator first = openFolder(root, error);
    if (error)
    {
        return std::nullopt;
    }
    open.push_back(std::move(first));
    std::size_t visited = 0;
    while (!open.empty())
    {
        fs::directory_iterator &listing = open.back();
        if (listing == fs::directory_iterator())
        {
            open.pop_back();
            continue;
        }
        const fs::directory_entry entry = *listing;
        listing.increment(error);
        if (const auto hook = scanHook.load(std::memory_order_relaxed); hook && !error)
        {
            error = hook(detail::ScanStep::Next, entry.path().parent_path());
        }
        if (error)
        {
            // The rest of this folder can't be listed: skip it, but still visit `entry`.
            open.pop_back();
            error.clear();
        }
        if (++visited > kMaxScanEntries)
        {
            return std::nullopt;
        }
        const fs::path &path = entry.path();
        if (name.matches(path.filename()) && candidates.check(path) == Check::Matches)
        {
            return path;
        }
        // Into a folder, but not through a symlink: symlink_status doesn't follow it.
        if (fs::is_directory(entry.symlink_status(error)) && !error)
        {
            fs::directory_iterator inside = openFolder(path, error);
            if (!error)
            {
                open.push_back(std::move(inside));
            }
        }
        error.clear();
    }
    return std::nullopt;
}

/// The first spelling of UTF-8 `text` under `folder` that matches (as saved, NFC, then NFD).
std::optional<fs::path> anySpelling(Candidates &candidates, const fs::path &folder,
                                    const std::string &text)
{
    for (const auto &spelling : spellings(text))
    {
        if (const auto tail = fromUtf8(spelling))
        {
            const fs::path candidate = folder / *tail;
            if (candidates.check(candidate) == Check::Matches)
            {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

/// What step 1 found at the saved path.
struct AtSaved
{
    std::optional<fs::path> match;   // a spelling holding the entry's file
    std::optional<fs::path> changed; // otherwise, the first spelling holding other content
};

/// Step 1: the saved path, in each spelling.
AtSaved atSavedPath(Candidates &candidates, const std::string &absolutePath)
{
    AtSaved out;
    const auto saved = fromUtf8(absolutePath);
    if (!saved || saved->empty())
    {
        return out;
    }
    for (const auto &spelling : spellings(absolutePath))
    {
        if (const auto candidate = fromUtf8(spelling))
        {
            const Check atSaved = candidates.check(*candidate);
            if (atSaved == Check::Matches)
            {
                out.match = *candidate;
                return out;
            }
            if (atSaved == Check::Differs && !out.changed)
            {
                out.changed = *candidate;
            }
        }
    }
    return out;
}

/// The last component of a folder given as text, after either separator, ignoring trailing ones.
std::string lastComponent(const std::string &folder)
{
    std::size_t end = folder.size();
    while (end > 0 && (folder[end - 1] == '/' || folder[end - 1] == '\\'))
    {
        --end;
    }
    const std::size_t start = folder.find_last_of("/\\", end == 0 ? 0 : end - 1);
    const std::size_t from = start == std::string::npos || start >= end ? 0 : start + 1;
    return folder.substr(from, end - from);
}

/// The captures folder (the setting, or the default), as a path; empty if neither is known.
fs::path capturesFolderOf(const core::MachineSettings &settings)
{
    if (!settings.capturesFolder.empty())
    {
        return fromUtf8(settings.capturesFolder).value_or(fs::path());
    }
    return defaultCapturesFolder();
}

/// Step 4: the captures folder plus the name of the saved path, when the saved path's folder is
/// a captures folder: named "Captures" (the default's name, on any machine) or as the setting's
/// folder is.
std::optional<fs::path> inCapturesFolder(Candidates &candidates, const std::string &absolutePath,
                                         const core::MachineSettings &settings)
{
    const std::size_t separator = lastSeparator(absolutePath);
    if (separator == std::string::npos || separator + 1 >= absolutePath.size())
    {
        return std::nullopt;
    }
    const std::string savedFolder = lastComponent(absolutePath.substr(0, separator + 1));
    const fs::path folder = capturesFolderOf(settings);
    if (folder.empty())
    {
        return std::nullopt;
    }
    const auto folderText = toUtf8(folder);
    const std::string ours = folderText ? lastComponent(*folderText) : std::string();
    if (savedFolder != "Captures" && (ours.empty() || savedFolder != ours))
    {
        return std::nullopt;
    }
    return anySpelling(candidates, folder, absolutePath.substr(separator + 1));
}
} // namespace

SearchResult findSample(const core::MediaEntry &entry, const core::MachineSettings &settings,
                        std::span<const fs::path> contentRoots)
{
    Candidates candidates(entry);
    const fs::path saved = fromUtf8(entry.absolutePath).value_or(fs::path());
    const auto found = [&](const fs::path &candidate) {
        const fs::path where = candidate.lexically_normal();
        return SearchResult{SearchOutcome::Found, where, where != saved.lexically_normal()};
    };

    // 1. The saved path, in each spelling. The first spelling that holds other content is where
    // the file changed.
    const AtSaved atSaved = atSavedPath(candidates, entry.absolutePath);
    if (atSaved.match)
    {
        return found(*atSaved.match);
    }
    const std::optional<fs::path> &changed = atSaved.changed;

    // 2. Each content root plus the content-relative path, in the roots' order.
    if (!entry.contentPath.empty())
    {
        for (const fs::path &root : contentRoots)
        {
            if (const auto match = anySpelling(candidates, root, entry.contentPath))
            {
                return found(*match);
            }
        }
    }

    // 3. The sample folder and the relative path.
    const auto sampleFolder =
        settings.sampleFolder.empty() ? std::nullopt : fromUtf8(settings.sampleFolder);
    if (sampleFolder && !entry.relativePath.empty())
    {
        if (const auto match = anySpelling(candidates, *sampleFolder, entry.relativePath))
        {
            return found(*match);
        }
    }

    // 4. The captures folder, for a file saved in one.
    if (const auto match = inCapturesFolder(candidates, entry.absolutePath, settings))
    {
        return found(*match);
    }

    // 5. By name: in the saved path's folder, the last folder loaded from, then the scan.
    const std::size_t separator = lastSeparator(entry.absolutePath);
    const std::string name = separator == std::string::npos
                                 ? entry.absolutePath
                                 : entry.absolutePath.substr(separator + 1);
    if (fromUtf8(name) && !name.empty() && name != "." && name != "..")
    {
        if (separator != std::string::npos)
        {
            if (const auto folder = fromUtf8(entry.absolutePath.substr(0, separator + 1)))
            {
                if (const auto match = anySpelling(candidates, *folder, name))
                {
                    return found(*match);
                }
            }
        }
        if (!settings.lastLoadFolder.empty())
        {
            if (const auto folder = fromUtf8(settings.lastLoadFolder))
            {
                if (const auto match = anySpelling(candidates, *folder, name))
                {
                    return found(*match);
                }
            }
        }
        if (sampleFolder && sampleFolder->is_absolute())
        {
            if (const auto match = scan(*sampleFolder, NameMatcher(name), candidates))
            {
                return found(*match);
            }
        }
    }

    if (changed)
    {
        return {SearchOutcome::Changed, changed->lexically_normal(), false};
    }
    return {SearchOutcome::Missing, {}, false};
}

SearchResult findFiledAudio(std::string_view sha256Hex, uint64_t size,
                            std::string_view absolutePath, const core::MachineSettings &settings)
{
    core::MediaEntry entry;
    entry.sha256Hex = std::string(sha256Hex);
    entry.fileSize = size;
    entry.absolutePath = std::string(absolutePath);
    Candidates candidates(entry);
    const fs::path saved = fromUtf8(entry.absolutePath).value_or(fs::path());
    const auto found = [&](const fs::path &candidate) {
        const fs::path where = candidate.lexically_normal();
        return SearchResult{SearchOutcome::Found, where, where != saved.lexically_normal()};
    };

    const AtSaved atSaved = atSavedPath(candidates, entry.absolutePath);
    if (atSaved.match)
    {
        return found(*atSaved.match);
    }
    // Moved with its captures folder, whatever the folder is called on this machine.
    const std::size_t separator = lastSeparator(entry.absolutePath);
    if (separator != std::string::npos && separator + 1 < entry.absolutePath.size())
    {
        const fs::path folder = capturesFolderOf(settings);
        if (!folder.empty())
        {
            if (const auto match =
                    anySpelling(candidates, folder, entry.absolutePath.substr(separator + 1)))
            {
                return found(*match);
            }
        }
    }
    if (atSaved.changed)
    {
        return {SearchOutcome::Changed, atSaved.changed->lexically_normal(), false};
    }
    return {SearchOutcome::Missing, {}, false};
}

namespace detail
{
void setScanHook(ScanHook hook) { scanHook.store(hook); }
unsigned candidateHashCount() { return candidateHashes.load(); }
} // namespace detail
} // namespace gg::media
