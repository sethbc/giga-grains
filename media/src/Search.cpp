// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Search.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <utf8proc.h>

#include "TestHooks.h"
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

/// Step 3c: the first file under `root` whose name `name` matches and whose content matches, or
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
} // namespace

SearchResult findSample(const core::MediaEntry &entry, const core::MachineSettings &settings)
{
    Candidates candidates(entry);
    const fs::path saved = fromUtf8(entry.absolutePath).value_or(fs::path());
    const auto found = [&](const fs::path &candidate) {
        const fs::path where = candidate.lexically_normal();
        return SearchResult{SearchOutcome::Found, where, where != saved.lexically_normal()};
    };
    // The first spelling of `text` under `folder` that matches.
    const auto anySpelling = [&](const fs::path &folder,
                                 const std::string &text) -> std::optional<fs::path> {
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
    };

    // 1. The saved path, in each spelling. The first spelling that holds other content is where
    // the file changed.
    std::optional<fs::path> changed;
    if (!saved.empty())
    {
        for (const auto &spelling : spellings(entry.absolutePath))
        {
            if (const auto candidate = fromUtf8(spelling))
            {
                const Check atSaved = candidates.check(*candidate);
                if (atSaved == Check::Matches)
                {
                    return found(*candidate);
                }
                if (atSaved == Check::Differs && !changed)
                {
                    changed = *candidate;
                }
            }
        }
    }

    // 2. The sample folder and the relative path.
    const auto sampleFolder =
        settings.sampleFolder.empty() ? std::nullopt : fromUtf8(settings.sampleFolder);
    if (sampleFolder && !entry.relativePath.empty())
    {
        if (const auto match = anySpelling(*sampleFolder, entry.relativePath))
        {
            return found(*match);
        }
    }

    // 3. By name: in the saved path's folder, the last folder loaded from, then the scan.
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
                if (const auto match = anySpelling(*folder, name))
                {
                    return found(*match);
                }
            }
        }
        if (!settings.lastLoadFolder.empty())
        {
            if (const auto folder = fromUtf8(settings.lastLoadFolder))
            {
                if (const auto match = anySpelling(*folder, name))
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

namespace detail
{
void setScanHook(ScanHook hook) { scanHook.store(hook); }
unsigned candidateHashCount() { return candidateHashes.load(); }
} // namespace detail
} // namespace gg::media
