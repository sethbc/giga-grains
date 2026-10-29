// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "SampleLoader.h"

#include <cassert>
#include <exception>
#include <new>
#include <utility>

#include "gg/media/Sample.h"
#include "gg/media/Search.h"
#include "gg/media/SettingsFile.h"
#include "gg/media/Utf8Path.h"

#include "SearchCache.h"

namespace gg::plugin
{
namespace
{
namespace fs = std::filesystem;

/// The last component of a UTF-8 path, after either separator, so a path saved on Windows still
/// gives its name elsewhere.
std::string nameOf(const std::string &utf8Path)
{
    const auto separator = utf8Path.find_last_of("/\\");
    return separator == std::string::npos ? utf8Path : utf8Path.substr(separator + 1);
}

/// `path` as UTF-8; empty if it can't be converted (a wide path that isn't valid UTF-16).
std::string utf8(const fs::path &path) { return media::toUtf8(path).value_or(std::string()); }

/// The path UTF-8 `text` names; empty if it can't be converted (text that isn't valid UTF-8, on
/// Windows).
fs::path pathOf(const std::string &text) { return media::fromUtf8(text).value_or(fs::path()); }

/// An exception as a message for the user.
std::string describe(const std::exception_ptr &error, const std::string &fileName)
{
    try
    {
        std::rethrow_exception(error);
    }
    catch (const std::bad_alloc &)
    {
        return "not enough memory to load " + fileName;
    }
    catch (const std::exception &e)
    {
        return "can't load " + fileName + ": " + e.what();
    }
    catch (...)
    {
        return "can't load " + fileName;
    }
}

constexpr const char *kDifferentAudio = "different audio than the saved sample";
constexpr const char *kMissing = "not found";
// Error: a file is there, but can't be read (carry-over [194]).
constexpr const char *kUnreadable = "the file at the saved path can't be read";
constexpr const char *kChanged = "the file at the saved path has changed";
constexpr const char *kNewerVersion = "sample from a newer version";
// Short enough to fit any std::string's own buffer, so assigning it never allocates.
constexpr const char *kCantLoad = "can't load";
} // namespace

SampleLoader::SampleLoader(std::filesystem::path settingsFile, Callbacks callbacks)
    : settingsFile_(std::move(settingsFile)), callbacks_(std::move(callbacks))
{
}

SampleLoader::~SampleLoader() { stopWorker(true); }

void SampleLoader::requestLoad(std::filesystem::path file) noexcept
{
    std::string fileName;
    try
    {
        fileName = utf8(file.filename());
        submit({0, JobKind::Load, std::move(file), readSettings(), {}});
        status_ = {SampleState::Loading, fileName, {}, 0.0, 0, 0.0};
    }
    catch (...)
    {
        setRequestErrorNoThrow(std::current_exception(), fileName);
    }
}

void SampleLoader::requestRelink(std::filesystem::path file) noexcept
{
    std::string fileName;
    try
    {
        fileName = utf8(file.filename());
        std::string expected = entry_.has_value() ? entry_->sha256Hex : std::string();
        submit({0, JobKind::Relink, std::move(file), readSettings(), std::move(expected)});
        status_ = {SampleState::Loading, fileName, {}, 0.0, 0, 0.0};
    }
    catch (...)
    {
        setRequestErrorNoThrow(std::current_exception(), fileName);
    }
}

bool SampleLoader::setSampleFolder(const std::filesystem::path &folder)
{
    // Main thread (ruling R16), as apply() saves the last load folder.
    if (settingsFile_.empty())
    {
        return false;
    }
    core::MachineSettings settings = readSettings();
    settings.sampleFolder = utf8(folder);
    const bool saved = media::saveSettings(settingsFile_, settings);
    SearchCache::process().clear(); // searched again in the new folder (carry-over [192])
    return saved;
}

SampleLoader::Restored SampleLoader::restore(const std::optional<core::MediaEntry> &entry,
                                             std::optional<std::string> newerMedia) noexcept
{
    std::shared_ptr<const PreparedSource> source;
    std::optional<Result> stale;
    std::string fileName;
    try
    {
        // Every request so far is superseded: the session decides the sample now. A result the
        // worker finished is dropped after the lock is released.
        {
            std::lock_guard lock(mutex_);
            job_.reset();
            stale.swap(result_);
            latestRequest_ = ++lastRequest_;
        }
        // A newer build's manifest: no sample this build can read. Kept for the next save, and
        // silent (INTENT §18).
        newerMedia_ = std::move(newerMedia);
        if (newerMedia_.has_value())
        {
            pending_.reset();
            restorePending_ = false;
            source_.reset();
            entry_.reset();
            entryUnavailable_ = false;
            status_ = {SampleState::Newer, {}, kNewerVersion, 0.0, 0, 0.0};
            return {};
        }
        // Ruling R19: the session's sample is the one in memory. Kept as it is, entry and all;
        // only the session's values change.
        if (entry.has_value() && entry_.has_value() && entry->sha256Hex == entry_->sha256Hex &&
            entry->fileSize == entry_->fileSize &&
            (source_ != nullptr || (restorePending_ && pending_ != nullptr)))
        {
            setLoadedStatus({});
            return {nullptr, true};
        }
        pending_.reset();
        restorePending_ = false;
        source_.reset();
        fileName = entry.has_value() ? nameOf(entry->absolutePath) : std::string();
        entry_ = entry;
        entryUnavailable_ = false;
        status_ = {};
        if (entry.has_value())
        {
            source = findAndLoad(activeRate_);
        }
    }
    catch (...)
    {
        setErrorNoThrow(std::current_exception(), fileName);
        source.reset();
        pending_.reset();
        restorePending_ = false;
    }
    // Active: the plugin offers it with the session's values (D8); nullptr when there is
    // nothing to play, so a missing sample is silent. Inactive: activate() converts the pending
    // buffer.
    source_ = source;
    return {std::move(source), false};
}

std::shared_ptr<const PreparedSource> SampleLoader::activate(double sampleRate) noexcept
{
    // A job under way finishes, and what the worker finished applies, as inactive: a decoded
    // buffer is converted below, and a source prepared at another rate converted again.
    stopWorker(false);
    applyResults();
    activeRate_ = sampleRate;
    std::string fileName;
    try
    {
        fileName = entry_.has_value() ? nameOf(entry_->absolutePath) : std::string();
        {
            std::lock_guard lock(mutex_);
            workerRate_ = sampleRate;
        }
        if (restorePending_)
        {
            // D8: converted here, so the first block after activate() has it.
            restorePending_ = false;
            source_ = prepare(std::move(pending_), sampleRate);
        }
        else if (source_ != nullptr && source_->buffer().sampleRate() != sampleRate)
        {
            convertNow(sampleRate); // ruling R18
        }
        else if (entry_.has_value() && fileUnavailable())
        {
            // The file may have come back since (carry-over [194]): looked for again, as the
            // restore did, or, for a source kept from before, checked at the saved path.
            if (source_ == nullptr)
            {
                source_ = findAndLoad(sampleRate);
            }
            else
            {
                convertNow(sampleRate);
            }
        }
    }
    catch (...)
    {
        setErrorNoThrow(std::current_exception(), fileName);
        source_.reset();
    }
    pending_.reset();
    restorePending_ = false;
    try
    {
        bool waiting = false;
        {
            std::lock_guard lock(mutex_);
            waiting = job_.has_value();
        }
        if (waiting)
        {
            startWorker();
        }
    }
    catch (...)
    {
        // The worker couldn't start: the request waits for the next one.
    }
    return source_;
}

void SampleLoader::convertNow(double rate) noexcept
{
    // The file again, as long as it is still the manifest's sample: converted, or only checked
    // when the source is at `rate` already. Otherwise the old source stays (it plays at the right
    // pitch), the entry stays, and the status says the file is missing, has changed or can't be
    // read (ruling R18).
    assert(entry_.has_value() && source_ != nullptr);
    std::string fileName;
    try
    {
        fileName = nameOf(entry_->absolutePath);
        const fs::path file = pathOf(entry_->absolutePath);
        // Another size is other content, without reading it all to hash (a size the file system
        // can't give goes on to the hash, which says why).
        std::error_code sizeError;
        const auto size = fs::file_size(file, sizeError);
        if (!sizeError && size != entry_->fileSize)
        {
            setUnavailable(fileName, file, true);
            return;
        }
        const std::optional<media::FileHash> hash = media::hashFile(file);
        if (!hash.has_value() || hash->sha256Hex != entry_->sha256Hex ||
            hash->size != entry_->fileSize)
        {
            setUnavailable(fileName, file, hash.has_value());
            return;
        }
        if (source_->buffer().sampleRate() != rate)
        {
            media::LoadedSample loaded = media::loadSample(file, readSettings(), *hash);
            if (!loaded.error.empty())
            {
                setError(fileName, std::move(loaded.error));
                return;
            }
            source_ = prepare(std::move(loaded.decoded), rate);
        }
        if (fileUnavailable())
        {
            setLoadedStatus({}); // it has come back
        }
    }
    catch (...)
    {
        // No memory, or the file system: the old source stays, and the status says why.
        setErrorNoThrow(std::current_exception(), fileName);
    }
}

std::shared_ptr<const PreparedSource> SampleLoader::findAndLoad(double rate)
{
    const std::string fileName = nameOf(entry_->absolutePath);
    const fs::path saved = pathOf(entry_->absolutePath);
    const core::MachineSettings settings = readSettings();
    const media::SearchResult found =
        SearchCache::process().find(*entry_, settings, SearchCache::Clock::now());
    if (found.outcome != media::SearchOutcome::Found)
    {
        // Changed: findSample read the file at the saved path. Missing with a file there: it
        // couldn't read it.
        setUnavailable(fileName, saved, found.outcome == media::SearchOutcome::Changed);
        return nullptr;
    }
    // findSample hashed the file: its fingerprint is the entry's, so loadSample needn't read it a
    // third time.
    media::LoadedSample loaded = media::loadSample(
        found.path, settings, media::FileHash{entry_->sha256Hex, entry_->fileSize});
    if (!loaded.error.empty())
    {
        setError(fileName, std::move(loaded.error));
        return nullptr;
    }
    if (found.moved)
    {
        // The next save records where it is now (spec §6.7).
        entry_ = std::move(loaded.entry);
        if (callbacks_.markDirty)
        {
            callbacks_.markDirty();
        }
    }
    std::shared_ptr<const PreparedSource> source;
    if (rate > 0.0)
    {
        source = prepare(std::move(loaded.decoded), rate);
    }
    else
    {
        pending_ = std::move(loaded.decoded);
        restorePending_ = true;
    }
    setLoadedStatus({});
    return source;
}

void SampleLoader::deactivate() noexcept
{
    activeRate_ = 0.0;
    try
    {
        std::lock_guard lock(mutex_);
        workerRate_ = 0.0;
    }
    catch (...)
    {
        // A mutex that can't lock: the worker is stopped below anyway.
    }
    stopWorker(true); // a job under way is queued again, for the next activation
    applyResults();
}

void SampleLoader::onMainThread() noexcept { applyResults(); }

void SampleLoader::submit(Job job)
{
    job.id = ++lastRequest_;
    {
        std::lock_guard lock(mutex_);
        latestRequest_ = job.id;
        job_ = std::move(job); // replaces a waiting one
    }
    wake_.notify_one();
    startWorker();
}

void SampleLoader::startWorker()
{
    if (!worker_.joinable())
    {
        worker_ = std::thread(&SampleLoader::workerLoop, this);
    }
}

void SampleLoader::stopWorker(bool cancel) noexcept
{
    if (!worker_.joinable())
    {
        return;
    }
    cancel_.store(cancel, std::memory_order_relaxed); // the join orders it with the worker's reads
    try
    {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        worker_.join();
        std::lock_guard lock(mutex_);
        stop_ = false;
    }
    catch (...)
    {
        // Only a mutex or a join that fails at the system level gets here.
    }
    cancel_.store(false, std::memory_order_relaxed);
}

void SampleLoader::workerLoop()
{
    std::unique_lock lock(mutex_);
    for (;;)
    {
        wake_.wait(lock, [this] { return stop_ || job_.has_value(); });
        if (stop_)
        {
            return; // a waiting job stays queued for the next start
        }
        Job job = std::move(*job_);
        job_.reset();
        const double rate = workerRate_;
        lock.unlock();

        // Whatever fails, the request gets an answer, an error at worst, so its status leaves
        // Loading.
        Result result;
        result.id = job.id;
        try
        {
            result.fileName = utf8(job.file.filename());
            result = run(job, rate);
        }
        catch (...)
        {
            result.prepared.reset();
            result.decoded.reset();
            try
            {
                result.error = describe(std::current_exception(), result.fileName);
            }
            catch (...)
            {
                result.error = kCantLoad;
            }
        }
        if (result.cancelled)
        {
            // Stopped by deactivate(): it runs again when the worker next starts, unless a newer
            // request has replaced it (the destructor drops the queue with the loader).
            lock.lock();
            if (!job_.has_value() && job.id == latestRequest_)
            {
                job_ = std::move(job);
            }
            continue; // to the wait, which sees stop_
        }

        // Only the latest request's result is kept: a newer request supersedes any older one,
        // finished or not, so one slot is enough. Whichever result loses, this one if it was
        // superseded or the one it replaces, is freed after the lock is released: freeing a
        // large buffer takes time, and the main thread may be waiting for the mutex.
        std::optional<Result> slot(std::move(result));
        lock.lock();
        const bool current = slot->id == latestRequest_;
        if (current)
        {
            result_.swap(slot);
        }
        lock.unlock();
        slot.reset();
        if (current && callbacks_.requestCallback)
        {
            callbacks_.requestCallback();
        }
        lock.lock();
    }
}

SampleLoader::Result SampleLoader::run(const Job &job, double rate)
{
    Result result;
    result.id = job.id;
    result.fileName = utf8(job.file.filename());
    media::LoadedSample loaded = media::loadSample(job.file, job.settings, std::nullopt, &cancel_);
    if (cancel_.load(std::memory_order_relaxed))
    {
        result.cancelled = true;
        return result;
    }
    if (!loaded.error.empty())
    {
        result.error = std::move(loaded.error);
        return result;
    }
    result.differentAudio = job.kind == JobKind::Relink && !job.expectedSha256.empty() &&
                            loaded.entry.sha256Hex != job.expectedSha256;
    result.entry = std::move(loaded.entry);
    {
        // Superseded while decoding: abandoned before the conversion (spec §6.4).
        std::lock_guard lock(mutex_);
        if (job.id != latestRequest_)
        {
            return result;
        }
    }
    if (rate > 0.0)
    {
        result.prepared = prepare(std::move(loaded.decoded), rate, &cancel_);
        result.cancelled = result.prepared == nullptr;
    }
    else
    {
        result.decoded = std::move(loaded.decoded); // inactive: activate() converts it
    }
    return result;
}

void SampleLoader::applyResults() noexcept
{
    std::optional<Result> result;
    std::string fileName;
    try
    {
        {
            std::lock_guard lock(mutex_);
            result.swap(result_);
        }
        if (!result.has_value() || result->id != lastRequest_)
        {
            return; // none, or superseded after it finished
        }
        fileName = result->fileName;
        apply(*result);
    }
    catch (...)
    {
        setErrorNoThrow(std::current_exception(), fileName);
    }
}

void SampleLoader::apply(Result &result)
{
    if (!result.error.empty())
    {
        // The entry and the source stay as they were, and so does the name shown: the message
        // names the file that failed.
        setRequestError(result.fileName, std::move(result.error));
        return;
    }
    entry_ = std::move(result.entry);
    newerMedia_.reset(); // the sample loaded replaces a newer build's manifest
    setLoadedStatus(result.differentAudio ? kDifferentAudio : "");
    // A file loaded may be one another instance's restore found missing (carry-over [192]).
    SearchCache::process().clear();
    // The settings remember the folder loaded from (spec §6.7), on the main thread (R16).
    if (!settingsFile_.empty())
    {
        core::MachineSettings settings = readSettings();
        settings.lastLoadFolder = utf8(pathOf(entry_->absolutePath).parent_path());
        media::saveSettings(settingsFile_, settings);
    }
    if (callbacks_.markDirty)
    {
        callbacks_.markDirty();
    }
    ++resultsApplied_;
    if (result.prepared != nullptr)
    {
        // At the active rate: a job runs at the rate in effect when it starts, and activate()
        // waits for one under way while deactivate() cancels it, so the rate can't change under
        // it. Applied while inactive, activate() converts it if it must.
        pending_.reset();
        restorePending_ = false;
        setSource(std::move(result.prepared));
    }
    else if (result.decoded != nullptr)
    {
        // Decoded while inactive. Still inactive: activate() converts it. A decoded result is
        // applied while active only if an activation came between the worker's posting and this
        // call without applying it, which activate() rules out; converted here all the same.
        if (activeRate_ > 0.0)
        {
            setSource(prepare(std::move(result.decoded), activeRate_));
        }
        else
        {
            source_.reset(); // the old sample; activate() converts the new one
            pending_ = std::move(result.decoded);
            restorePending_ = true;
        }
    }
}

void SampleLoader::setSource(std::shared_ptr<const PreparedSource> source)
{
    source_ = std::move(source);
    if (activeRate_ > 0.0 && callbacks_.offer)
    {
        callbacks_.offer(source_);
    }
}

std::shared_ptr<const PreparedSource> SampleLoader::prepare(std::shared_ptr<SampleBuffer> decoded,
                                                            double rate,
                                                            const std::atomic<bool> *cancel) const
{
    auto source = media::prepare(std::move(decoded), rate, cancel);
    if (source != nullptr && preparedObserver_)
    {
        preparedObserver_(source);
    }
    return source;
}

void SampleLoader::setLoadedStatus(std::string message)
{
    const core::MediaEntry &e = *entry_;
    entryUnavailable_ = false;
    status_ = {SampleState::Loaded,
               nameOf(e.absolutePath),
               std::move(message),
               e.sampleRate > 0.0 ? static_cast<double>(e.frames) / e.sampleRate : 0.0,
               e.channels,
               e.sampleRate};
}

void SampleLoader::setError(std::string fileName, std::string message)
{
    status_ = {SampleState::Error, std::move(fileName), std::move(message), 0.0, 0, 0.0};
}

void SampleLoader::setUnavailable(const std::string &fileName, const fs::path &file, bool read)
{
    std::error_code error;
    entryUnavailable_ = true;
    if (read)
    {
        // Something at the saved path that isn't the saved audio has changed.
        status_ = {SampleState::Changed, fileName, kChanged, 0.0, 0, 0.0, true};
    }
    else if (file.is_absolute() && fs::is_regular_file(file, error))
    {
        // Present, so neither missing nor known to have changed.
        status_ = {SampleState::Error, fileName, kUnreadable, 0.0, 0, 0.0, true};
    }
    else
    {
        status_ = {SampleState::Missing, fileName, kMissing, 0.0, 0, 0.0, true};
    }
}

void SampleLoader::setRequestError(const std::string &failedName, std::string message)
{
    // A failed request doesn't make the saved sample available: if its file was unavailable
    // before (so Relink was on offer), it still is.
    setError(entry_.has_value() ? nameOf(entry_->absolutePath) : failedName, std::move(message));
    status_.relinkable = entryUnavailable_ && entry_.has_value();
}

void SampleLoader::setRequestErrorNoThrow(const std::exception_ptr &error,
                                          const std::string &failedName) noexcept
{
    try
    {
        setRequestError(failedName, describe(error, failedName));
    }
    catch (...)
    {
        status_.state = SampleState::Error;
        status_.fileName.clear();
        status_.message = kCantLoad; // fits the string's own buffer: no allocation
    }
}

void SampleLoader::setErrorNoThrow(const std::exception_ptr &error,
                                   const std::string &fileName) noexcept
{
    try
    {
        setError(fileName, describe(error, fileName));
    }
    catch (...)
    {
        status_.state = SampleState::Error;
        status_.relinkable = false;
        status_.fileName.clear();
        status_.message = kCantLoad; // fits the string's own buffer: no allocation
    }
}

core::MachineSettings SampleLoader::readSettings() const
{
    return settingsFile_.empty() ? core::MachineSettings{} : media::loadSettings(settingsFile_);
}
} // namespace gg::plugin
