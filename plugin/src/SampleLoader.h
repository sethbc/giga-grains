// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_SAMPLELOADER_H
#define GG_PLUGIN_SAMPLELOADER_H

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"

namespace gg::plugin
{
/// What the instrument's sample is doing, for the editor (spec §6.5).
enum class SampleState
{
    None,    ///< No sample.
    Loading, ///< A load or relink the user asked for is under way.
    Loaded,  ///< The manifest's sample is loaded.
    Missing, ///< A restored session's sample wasn't found (spec §6.7); silent.
    Changed, ///< Not found, and the file at the saved path has other content; silent.
    Error,   ///< The last load failed; `message` says why. A sample loaded before keeps playing.
};

struct SampleStatus
{
    SampleState state{SampleState::None};
    std::string fileName; ///< The file's name (UTF-8): the manifest's, or the one loading.
    std::string message;  ///< For the user: why it failed, or a note on a loaded sample.
    double seconds{0.0};  ///< Loaded: the file's length.
    uint32_t channels{0}; ///< Loaded: the file's channels, as stored.
    double fileRate{0.0}; ///< Loaded: the file's sample rate.
};

/// The instrument's sample loader (spec §6.4–§6.7): the manifest entry, the source the engine
/// plays, the user's loads and relinks on a background thread, and the synchronous session
/// restore (D8). Owned by GigaGrainsPlugin, the instrument's only; the FX has none.
///
/// **Threads.** Every public function is main thread only. One worker std::thread per instance
/// decodes, hashes, converts and prepares a load (gg::media::loadSample, then prepare); it waits
/// on a condition variable between jobs and never reads or writes the settings file (ruling R16).
/// The worker starts with the first job and stops in deactivate() and the destructor, each of
/// which waits for a job in progress to finish; a job still waiting stays queued, and runs when
/// the worker next starts (a request, or activate()). The worker's finished result goes back
/// under the loader's mutex, and Callbacks::requestCallback asks the host for on_main_thread,
/// where onMainThread() applies it. The audio thread never touches the loader: sources reach it
/// through Callbacks::offer, the active Processor's SourceExchange.
///
/// **Requests and supersession.** Each request (load, relink, the reconversion after a rate
/// change, a restore) supersedes the ones before it: a waiting job is replaced, and the result of
/// one under way is dropped, by the worker when it notices or by onMainThread(). So a result only
/// ever applies if nothing was asked for after it. At most three sources exist at once
/// (spec §6.4): the engine's draining and playing ones, and one in flight.
///
/// **The manifest entry** (spec §6.6) is the requested sample, not the one playing: only a
/// successful load or relink replaces it (and a restore, which sets it to the session's). It
/// stays as it is while the sample is loading, missing, changed or failed to load, so a session
/// saved then still names it.
///
/// **The rate.** A source is prepared at the active sample rate. A job the worker starts while
/// the plugin is inactive keeps the decoded buffer instead, as a restore made while inactive does
/// (restorePending): activate() then converts it at the new rate, on the main thread, before it
/// returns (D8). A reactivation at another rate with nothing pending gives the new Processor the
/// source it has at once (the engine scales each grain's rate by source rate / output rate, so it
/// plays at the right pitch) and converts the file again in the background; the decoded original
/// isn't kept (spec §6.4), so that decodes the file again.
class SampleLoader
{
  public:
    /// How the loader reaches the plugin.
    struct Callbacks
    {
        /// Asks the host for an on_main_thread call (clap_host.request_callback). Called from the
        /// worker thread.
        std::function<void()> requestCallback;
        /// Marks the session state dirty (clap_host_state.mark_dirty). Main thread.
        std::function<void()> markDirty;
        /// Offers a source (nullptr: none) to the active Processor (SourceExchange::offer). Main
        /// thread, only while active.
        std::function<void(std::shared_ptr<const PreparedSource>)> offer;
    };

    /// `settingsFile` is the per-machine settings file (spec §6.7) this instance reads, and
    /// updates after a load. Nothing is read until a request or a restore needs it.
    SampleLoader(std::filesystem::path settingsFile, Callbacks callbacks);
    /// Stops the worker, waiting for a job in progress to finish.
    ~SampleLoader();
    SampleLoader(const SampleLoader &) = delete;
    SampleLoader &operator=(const SampleLoader &) = delete;

    /// Loads `file` (UTF-8 path) in the background, superseding any request before it. The status
    /// is Loading until onMainThread() applies the result: Loaded, with the manifest entry
    /// replaced, the settings' last load folder updated and the state marked dirty; or Error,
    /// with the entry and the source as they were.
    void requestLoad(std::filesystem::path file);
    /// As requestLoad. When the file's fingerprint isn't the manifest entry's, it still loads,
    /// as a replacement, and the status message says it is different audio than the saved
    /// sample (spec §6.7).
    void requestRelink(std::filesystem::path file);

    /// The status (see SampleState).
    SampleStatus status() const { return status_; }
    /// The manifest entry: what the session saves (spec §6.6).
    const std::optional<core::MediaEntry> &entry() const noexcept { return entry_; }

    /// The session restore (D8), inside stateLoad: supersedes every request, sets the manifest
    /// entry to `entry` and, with one, looks for its file (gg::media::findSample) and decodes it
    /// on this thread. Active, it converts it to the active rate and offers it (nullptr when
    /// there is no sample, or it can't be found or loaded, so the instrument goes silent);
    /// inactive, it keeps the decoded buffer for activate(). A file found away from the saved
    /// path updates the entry and marks the state dirty. Status: Loaded, Missing, Changed, Error,
    /// or None without an entry. Never throws.
    void restore(const std::optional<core::MediaEntry> &entry) noexcept;

    /// The plugin's activate(), before it returns. Converts a pending decoded buffer at
    /// `sampleRate`, here, and returns the source the new Processor starts with
    /// (Processor::setSourceNow); nullptr for none. Starts a background conversion when the
    /// source it returns is at another rate, and starts the worker when a job waits. Never
    /// throws: a failed conversion leaves no source and the status Error.
    std::shared_ptr<const PreparedSource> activate(double sampleRate) noexcept;
    /// The plugin's deactivate(): stops the worker, waiting for a job in progress, and applies
    /// what it finished, as onMainThread() would but with nothing offered.
    void deactivate() noexcept;

    /// clap_plugin.on_main_thread: applies a finished result, unless superseded.
    void onMainThread() noexcept;

    /// The results applied so far: loads, relinks and reconversions that reached the manifest or
    /// the source. For tests and diagnostics.
    uint32_t resultsApplied() const noexcept { return resultsApplied_; }
    /// The settings file given to the constructor.
    const std::filesystem::path &settingsFile() const noexcept { return settingsFile_; }

  private:
    enum class JobKind
    {
        Load,
        Relink,
        Reconvert, ///< The current sample's file again, at the active rate; the entry stays.
    };

    struct Job
    {
        uint64_t id{0};
        JobKind kind{JobKind::Load};
        std::filesystem::path file;
        core::MachineSettings settings;        ///< read on the main thread (ruling R16)
        std::shared_ptr<SampleBuffer> decoded; ///< when set, only converted, not loaded
        std::string expectedSha256;            ///< Relink, Reconvert: the entry's fingerprint
    };

    struct Result
    {
        uint64_t id{0};
        JobKind kind{JobKind::Load};
        std::string fileName;                  ///< the job's file, for messages
        core::MediaEntry entry;                ///< Load, Relink: the file's
        std::shared_ptr<SampleBuffer> decoded; ///< when not prepared (rate 0)
        std::shared_ptr<const PreparedSource> prepared;
        bool differentAudio{false}; ///< Relink: not the entry's fingerprint
        std::string error;          ///< non-empty on failure
    };

    /// Queues `job` as the only waiting one, starting the worker if needed.
    void submit(Job job);
    void startWorker();
    void stopWorker() noexcept;
    void workerLoop();
    /// Runs `job` at `rate` (0: inactive, keep the decoded buffer). Worker thread.
    Result run(Job job, double rate);
    /// Applies the finished result, unless superseded; offers its source only while active.
    void applyResults() noexcept;
    void apply(Result &result);
    /// Makes `source` the one in effect, offering it while active.
    void setSource(std::shared_ptr<const PreparedSource> source);
    /// Queues the reconversion of the current sample at the active rate, if its source is at
    /// another one.
    void reconvertIfNeeded();
    void setLoadedStatus(std::string message);
    void setError(std::string fileName, std::string message);
    core::MachineSettings readSettings() const;

    const std::filesystem::path settingsFile_;
    const Callbacks callbacks_;

    // Main thread.
    std::optional<core::MediaEntry> entry_;
    SampleStatus status_;
    std::shared_ptr<const PreparedSource> source_; // the source in effect, at its own rate
    std::shared_ptr<SampleBuffer> pending_;        // decoded, for activate() (restorePending)
    bool restorePending_{false};
    double activeRate_{0.0}; // 0 while inactive
    uint64_t lastRequest_{0};
    uint32_t resultsApplied_{0};

    // Shared with the worker, under mutex_.
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Job> job_;       // the waiting job
    std::optional<Result> result_; // finished, not yet applied: the latest request's
    uint64_t latestRequest_{0};    // lastRequest_, for the worker to drop superseded work
    double workerRate_{0.0};       // activeRate_, for the job the worker starts
    bool stop_{false};
    std::thread worker_; // main thread only starts and joins it
};
} // namespace gg::plugin

#endif // GG_PLUGIN_SAMPLELOADER_H
