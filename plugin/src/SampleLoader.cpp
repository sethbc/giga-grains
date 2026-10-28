// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "SampleLoader.h"

#include <exception>
#include <new>
#include <system_error>
#include <utility>

#include "gg/media/Sample.h"
#include "gg/media/Search.h"
#include "gg/media/SettingsFile.h"

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
std::string utf8(const fs::path &path)
{
    try
    {
        const auto text = path.u8string();
        return std::string(text.begin(), text.end());
    }
    catch (const std::system_error &)
    {
        return {};
    }
}

/// The path UTF-8 `text` names; empty if it can't be converted (text that isn't valid UTF-8, on
/// Windows).
fs::path pathOf(const std::string &text)
{
    try
    {
        return fs::path(std::u8string(text.begin(), text.end()));
    }
    catch (const std::system_error &)
    {
        return {};
    }
}

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
constexpr const char *kChanged = "the file at the saved path has changed";
} // namespace

SampleLoader::SampleLoader(std::filesystem::path settingsFile, Callbacks callbacks)
    : settingsFile_(std::move(settingsFile)), callbacks_(std::move(callbacks))
{
}

SampleLoader::~SampleLoader() { stopWorker(); }

void SampleLoader::requestLoad(std::filesystem::path file)
{
    const std::string fileName = utf8(file.filename());
    try
    {
        submit({0, JobKind::Load, std::move(file), readSettings(), nullptr, {}});
        status_ = {SampleState::Loading, fileName, {}, 0.0, 0, 0.0};
    }
    catch (...)
    {
        setError(fileName, describe(std::current_exception(), fileName));
    }
}

void SampleLoader::requestRelink(std::filesystem::path file)
{
    const std::string fileName = utf8(file.filename());
    try
    {
        std::string expected = entry_.has_value() ? entry_->sha256Hex : std::string();
        submit({0, JobKind::Relink, std::move(file), readSettings(), nullptr, std::move(expected)});
        status_ = {SampleState::Loading, fileName, {}, 0.0, 0, 0.0};
    }
    catch (...)
    {
        setError(fileName, describe(std::current_exception(), fileName));
    }
}

void SampleLoader::restore(const std::optional<core::MediaEntry> &entry) noexcept
{
    // Every request so far is superseded: the session decides the sample now.
    {
        std::lock_guard lock(mutex_);
        job_.reset();
        latestRequest_ = ++lastRequest_;
    }
    pending_.reset();
    restorePending_ = false;
    std::shared_ptr<const PreparedSource> source;
    const std::string fileName = entry.has_value() ? nameOf(entry->absolutePath) : std::string();
    try
    {
        entry_ = entry;
        status_ = {};
        if (entry.has_value())
        {
            const core::MachineSettings settings = readSettings();
            const media::SearchResult found = media::findSample(*entry, settings);
            if (found.outcome == media::SearchOutcome::Found)
            {
                media::LoadedSample loaded = media::loadSample(found.path, settings);
                if (!loaded.error.empty())
                {
                    setError(fileName, std::move(loaded.error));
                }
                else
                {
                    if (found.moved)
                    {
                        // The next save records where it is now (spec §6.7).
                        entry_ = std::move(loaded.entry);
                        if (callbacks_.markDirty)
                        {
                            callbacks_.markDirty();
                        }
                    }
                    if (activeRate_ > 0.0)
                    {
                        source = media::prepare(std::move(loaded.decoded), activeRate_);
                    }
                    else
                    {
                        pending_ = std::move(loaded.decoded);
                        restorePending_ = true;
                    }
                    setLoadedStatus({});
                }
            }
            else
            {
                const bool changed = found.outcome == media::SearchOutcome::Changed;
                status_ = {changed ? SampleState::Changed : SampleState::Missing,
                           fileName,
                           changed ? kChanged : kMissing,
                           0.0,
                           0,
                           0.0};
            }
        }
    }
    catch (...)
    {
        setError(fileName, describe(std::current_exception(), fileName));
        source.reset();
        pending_.reset();
        restorePending_ = false;
    }
    // Active: offered now, in the same stateLoad call that hands over the values (D8). nullptr
    // when there is nothing to play, so a missing sample is silent. Inactive: activate() does it.
    source_ = std::move(source);
    if (activeRate_ > 0.0)
    {
        try
        {
            callbacks_.offer(source_);
        }
        catch (...)
        {
            // Only allocation can fail here, and the source in play stays in play.
        }
    }
}

std::shared_ptr<const PreparedSource> SampleLoader::activate(double sampleRate) noexcept
{
    // A load the worker finished while inactive, not yet applied: applied first, as inactive,
    // so its decoded buffer is converted below rather than later in the background.
    applyResults();
    activeRate_ = sampleRate;
    {
        std::lock_guard lock(mutex_);
        workerRate_ = sampleRate;
    }
    if (restorePending_)
    {
        // D8: converted here, so the first block after activate() has it.
        try
        {
            source_ = media::prepare(std::move(pending_), sampleRate);
        }
        catch (...)
        {
            const std::string fileName = entry_.has_value() ? nameOf(entry_->absolutePath) : "";
            setError(fileName, describe(std::current_exception(), fileName));
            source_.reset();
        }
        pending_.reset();
        restorePending_ = false;
    }
    try
    {
        reconvertIfNeeded();
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
        // The worker couldn't start: the source plays at its own rate, at the right pitch.
    }
    return source_;
}

void SampleLoader::deactivate() noexcept
{
    activeRate_ = 0.0;
    {
        std::lock_guard lock(mutex_);
        workerRate_ = 0.0;
    }
    stopWorker();
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

void SampleLoader::stopWorker() noexcept
{
    if (!worker_.joinable())
    {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    try
    {
        worker_.join();
    }
    catch (...)
    {
        // join fails only for a thread that can't be joined, which joinable() ruled out.
    }
    std::lock_guard lock(mutex_);
    stop_ = false;
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
        result.kind = job.kind;
        try
        {
            result.fileName = utf8(job.file.filename());
            result = run(std::move(job), rate);
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
                result.error = "can't load"; // short enough not to allocate
            }
        }

        lock.lock();
        // Only the latest request's result is kept: a newer request supersedes any older one,
        // finished or not, so one slot is enough.
        const bool current = result.id == latestRequest_;
        if (current)
        {
            std::swap(result, result_.emplace());
        }
        lock.unlock();
        // A superseded result is dropped here, outside the lock: freeing a large buffer takes
        // time, and the main thread may be waiting for the mutex.
        result = Result{};
        if (current && callbacks_.requestCallback)
        {
            callbacks_.requestCallback();
        }
        lock.lock();
    }
}

SampleLoader::Result SampleLoader::run(Job job, double rate)
{
    Result result;
    result.id = job.id;
    result.kind = job.kind;
    result.fileName = utf8(job.file.filename());
    std::shared_ptr<SampleBuffer> decoded = std::move(job.decoded);
    if (decoded == nullptr)
    {
        media::LoadedSample loaded = media::loadSample(job.file, job.settings);
        if (!loaded.error.empty())
        {
            result.error = std::move(loaded.error);
            return result;
        }
        if (job.kind == JobKind::Reconvert && loaded.entry.sha256Hex != job.expectedSha256)
        {
            // Changed on disk since it was loaded: the source in play stays.
            result.error = kChanged;
            return result;
        }
        result.differentAudio = job.kind == JobKind::Relink && !job.expectedSha256.empty() &&
                                loaded.entry.sha256Hex != job.expectedSha256;
        result.entry = std::move(loaded.entry);
        decoded = std::move(loaded.decoded);
    }
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
        result.prepared = media::prepare(std::move(decoded), rate);
    }
    else
    {
        result.decoded = std::move(decoded); // inactive: activate() converts it
    }
    return result;
}

void SampleLoader::applyResults() noexcept
{
    std::optional<Result> result;
    {
        std::lock_guard lock(mutex_);
        result.swap(result_);
    }
    if (!result.has_value() || result->id != lastRequest_)
    {
        return; // none, or superseded after it finished
    }
    try
    {
        apply(*result);
    }
    catch (...)
    {
        setError(result->fileName, describe(std::current_exception(), result->fileName));
    }
}

void SampleLoader::apply(Result &result)
{
    if (!result.error.empty())
    {
        // The entry and the source stay as they were. A failed reconversion changes nothing the
        // user sees: the source in play keeps playing at its own rate.
        if (result.kind != JobKind::Reconvert)
        {
            setError(result.fileName, std::move(result.error));
        }
        return;
    }
    if (result.kind != JobKind::Reconvert)
    {
        entry_ = std::move(result.entry);
        setLoadedStatus(result.differentAudio ? kDifferentAudio : "");
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
    }
    ++resultsApplied_;
    if (result.prepared != nullptr)
    {
        pending_.reset();
        restorePending_ = false;
        setSource(std::move(result.prepared));
        reconvertIfNeeded(); // prepared at a rate the plugin has since left
    }
    else if (result.decoded != nullptr)
    {
        if (activeRate_ > 0.0)
        {
            // Decoded while inactive, finished after an activation: converted on the worker,
            // while the source in play keeps playing.
            submit({0, JobKind::Reconvert, {}, {}, std::move(result.decoded), {}});
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
    if (activeRate_ > 0.0)
    {
        callbacks_.offer(source_);
    }
}

void SampleLoader::reconvertIfNeeded()
{
    // Not while the user's request is under way: its result comes next, and supersedes this.
    if (activeRate_ <= 0.0 || source_ == nullptr || !entry_.has_value() ||
        status_.state == SampleState::Loading || source_->buffer().sampleRate() == activeRate_)
    {
        return;
    }
    submit({0, JobKind::Reconvert, pathOf(entry_->absolutePath), readSettings(), nullptr,
            entry_->sha256Hex});
}

void SampleLoader::setError(std::string fileName, std::string message)
{
    status_ = {SampleState::Error, std::move(fileName), std::move(message), 0.0, 0, 0.0};
}

void SampleLoader::setLoadedStatus(std::string message)
{
    const core::MediaEntry &e = *entry_;
    status_ = {SampleState::Loaded,
               nameOf(e.absolutePath),
               std::move(message),
               e.sampleRate > 0.0 ? static_cast<double>(e.frames) / e.sampleRate : 0.0,
               e.channels,
               e.sampleRate};
}

core::MachineSettings SampleLoader::readSettings() const
{
    return settingsFile_.empty() ? core::MachineSettings{} : media::loadSettings(settingsFile_);
}
} // namespace gg::plugin
