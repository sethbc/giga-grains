// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_SAMPLELOADER_H
#define GG_PLUGIN_SAMPLELOADER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
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
    Newer,   ///< A restored session's manifest is a newer version's (INTENT §18); silent.
};

struct SampleStatus
{
    SampleState state{SampleState::None};
    /// The file's name (UTF-8): the manifest's, or the one loading. After a load or relink that
    /// failed, still the manifest's when there is one (the sample that plays on), and `message`
    /// names the file that failed.
    std::string fileName;
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
/// The worker starts with the first job and stops in activate(), deactivate() and the
/// destructor. activate() waits for a job in progress to finish. deactivate() and the destructor
/// cancel it instead (carry-over [194]): the hash and the conversion stop between chunks, so
/// they return within a chunk's time, plus whatever is left of the file's decode, which runs to
/// its end (a WAV's takes milliseconds). deactivate() queues the job again, as if it hadn't
/// started; the destructor drops it. A job still waiting stays queued, and runs when the worker
/// next starts (a request, or the end of activate()). The
/// worker's finished result goes back under the loader's mutex, and Callbacks::requestCallback
/// asks the host for on_main_thread, where onMainThread() applies it. The audio thread never
/// touches the loader: sources reach it through the active Processor's SourceExchange.
///
/// **Requests and supersession.** Each request (a load, a relink, a restore) supersedes the ones
/// before it: a waiting job is replaced, and the result of one under way is dropped, by the
/// worker when it notices or by onMainThread(). So a result only ever applies if nothing was
/// asked for after it.
///
/// **How many sources exist.** The Processor's SourceExchange and the engine hold at most three
/// at once, and for up to one block four (Processor): the one draining, the one playing and one
/// waiting for adoption, and an offer the audio thread hasn't taken yet. Besides
/// those, the worker may briefly hold one it is building, and a superseded one until it notices,
/// both freed off the audio thread.
///
/// **The manifest entry** (spec §6.6) is the requested sample, not the one playing: only a
/// successful load or relink replaces it (and a restore, which sets it to the session's). It
/// stays as it is while the sample is loading, missing, changed or failed to load, so a session
/// saved then still names it. A session from a newer build's manifest has no entry but keeps the
/// manifest (newerMedia()), which a load or relink replaces in the same way.
///
/// **The rate** (D8; ruling R18). A source is prepared at the active sample rate. A job the
/// worker starts while the plugin is inactive keeps the decoded buffer instead, as a restore made
/// while inactive does (restorePending). activate() waits for a job under way and applies it,
/// then, on the main thread, before it returns: converts a pending decoded buffer at the new
/// rate, or, when the source it has is at another rate, decodes the file again (the decoded
/// original isn't kept, spec §6.4) and converts it. So the first block after any activation
/// renders the same whatever the machine's speed. If the file has gone or changed by then, the
/// old source stays (the engine scales each grain's rate by source rate / output rate, so it
/// plays at the right pitch), the manifest entry stays, and the status is Missing or Changed (or
/// Error, if it is there but can't be read).
///
/// **A file that comes back** (carry-over [194]). While the status says the manifest's file is
/// missing, has changed or can't be read, activate() looks for it again: with no source, it
/// searches as the restore did, and with a source kept from before, it checks the saved path
/// (converting the file if the rate changed, as above). Found, the sample is loaded, and the
/// status is Loaded.
///
/// **Searches** go through the process's SearchCache (carry-over [192]): a search that found
/// nothing is remembered for a while, so the instances of a project restoring one missing sample
/// scan the sample folder once. A load or relink applied, or the sample folder set, forgets them.
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
        /// Offers a loaded or relinked source to the active Processor (SourceExchange::offer).
        /// Main thread, only while active.
        std::function<void(std::shared_ptr<const PreparedSource>)> offer;
    };

    /// `settingsFile` is the per-machine settings file (spec §6.7) this instance reads, and
    /// updates after a load. Nothing is read until a request or a restore needs it.
    SampleLoader(std::filesystem::path settingsFile, Callbacks callbacks);
    /// Stops the worker, cancelling a job in progress (see Threads).
    ~SampleLoader();
    SampleLoader(const SampleLoader &) = delete;
    SampleLoader &operator=(const SampleLoader &) = delete;

    /// Loads `file` (UTF-8 path) in the background, superseding any request before it. The status
    /// is Loading until onMainThread() applies the result: Loaded, with the manifest entry
    /// replaced, the settings' last load folder updated and the state marked dirty; or Error,
    /// with the entry and the source as they were.
    void requestLoad(std::filesystem::path file) noexcept;
    /// As requestLoad. When the file's fingerprint isn't the manifest entry's, it still loads,
    /// as a replacement, and the status message says it is different audio than the saved
    /// sample (spec §6.7).
    void requestRelink(std::filesystem::path file) noexcept;
    /// Sets the per-machine sample folder (spec §6.5, §6.7) and saves the settings file. False if
    /// it can't be written.
    bool setSampleFolder(const std::filesystem::path &folder);

    /// The status (see SampleState).
    SampleStatus status() const { return status_; }
    /// The manifest entry: what the session saves (spec §6.6).
    const std::optional<core::MediaEntry> &entry() const noexcept { return entry_; }
    /// A newer build's manifest, as the restored session held it (core::SessionState's
    /// newerMedia): the session saves it back unchanged until a load or relink replaces it, or
    /// another restore. There is no entry meanwhile.
    const std::optional<std::string> &newerMedia() const noexcept { return newerMedia_; }

    /// What restore() leaves the plugin to offer with the session's values (D8).
    struct Restored
    {
        /// Active: the source to offer; nullptr when there is no sample, or it can't be found or
        /// loaded, so the instrument goes silent. Inactive: always nullptr.
        std::shared_ptr<const PreparedSource> source;
        /// The sample in memory is the session's own (ruling R19): offer the values alone and
        /// keep the source (SourceExchange::offerValues). `source` is then nullptr.
        bool keep{false};
    };

    /// The session restore (D8), inside stateLoad: supersedes every request, then:
    /// - When `entry` has the current manifest entry's fingerprint (SHA-256 and size) and its
    ///   sample is in memory, as a source or a decoded buffer waiting for activate() (ruling
    ///   R19: undo, A/B compare and preset recall restore the sample they already have): keeps
    ///   the entry and the sample, reads no file, and returns `keep`. Status: Loaded.
    /// - Otherwise sets the manifest entry to `entry` and, with one, looks for its file
    ///   (gg::media::findSample) and decodes it on this thread. Active, it converts it to the
    ///   active rate and returns it as `source`. Inactive, it keeps the decoded buffer for
    ///   activate(). A file found away from the saved path updates the entry and marks the state
    ///   dirty. Status: Loaded, Missing, Changed, Error, or None without an entry.
    /// - With `newerMedia` (a newer build's manifest, which has no `entry`): no sample, nothing
    ///   read, and nothing to offer; the manifest is kept for newerMedia(). Status: Newer, with
    ///   the message "sample from a newer version".
    /// Never throws.
    Restored restore(const std::optional<core::MediaEntry> &entry,
                     std::optional<std::string> newerMedia = std::nullopt) noexcept;

    /// The plugin's activate(), before it returns: waits for a job under way, applies what the
    /// worker finished, converts the sample at `sampleRate` here, or looks again for a file that
    /// wasn't at hand (see the class comment), and returns the source the new Processor starts
    /// with (Processor::setSourceNow); nullptr for none. Then restarts the worker if a job waits.
    /// Never throws: a failed conversion of a pending buffer leaves no source and the status Error.
    std::shared_ptr<const PreparedSource> activate(double sampleRate) noexcept;
    /// The plugin's deactivate(): stops the worker, cancelling a job in progress and queuing it
    /// again (see Threads), and applies what it finished, as onMainThread() would but with
    /// nothing offered.
    void deactivate() noexcept;

    /// clap_plugin.on_main_thread: applies a finished result, unless superseded.
    void onMainThread() noexcept;

    /// The results applied so far: loads and relinks that reached the manifest. For tests and
    /// diagnostics.
    uint32_t resultsApplied() const noexcept { return resultsApplied_; }
    /// The settings file given to the constructor.
    const std::filesystem::path &settingsFile() const noexcept { return settingsFile_; }
    /// For tests: called with every source the loader prepares, on the thread that prepares it
    /// (the worker or the main thread). Set it before any request or restore.
    void setPreparedObserver(std::function<void(const std::shared_ptr<const PreparedSource> &)> f)
    {
        preparedObserver_ = std::move(f);
    }

  private:
    enum class JobKind
    {
        Load,
        Relink,
    };

    struct Job
    {
        uint64_t id{0};
        JobKind kind{JobKind::Load};
        std::filesystem::path file;
        core::MachineSettings settings; ///< read on the main thread (ruling R16)
        std::string expectedSha256;     ///< Relink: the entry's fingerprint
    };

    struct Result
    {
        uint64_t id{0};
        std::string fileName;                  ///< the job's file, for messages
        core::MediaEntry entry;                ///< the file's
        std::shared_ptr<SampleBuffer> decoded; ///< when not prepared (rate 0)
        std::shared_ptr<const PreparedSource> prepared;
        bool differentAudio{false}; ///< Relink: not the entry's fingerprint
        std::string error;          ///< non-empty on failure
        bool cancelled{false};      ///< stopped part way by cancel_: nothing else is set
    };

    /// Queues `job` as the only waiting one, starting the worker if needed.
    void submit(Job job);
    void startWorker();
    /// Stops the worker. A job in progress finishes, or, with `cancel`, stops part way and is
    /// queued again, unless a newer request has replaced it.
    void stopWorker(bool cancel) noexcept;
    void workerLoop();
    /// Runs `job` at `rate` (0: inactive, keep the decoded buffer). Worker thread.
    Result run(const Job &job, double rate);
    /// Applies the finished result, unless superseded; offers its source only while active.
    void applyResults() noexcept;
    void apply(Result &result);
    /// Makes `source` the one in effect, offering it while active.
    void setSource(std::shared_ptr<const PreparedSource> source);
    /// gg::media::prepare, then the observer. nullptr only if `cancel` was set part way.
    std::shared_ptr<const PreparedSource> prepare(std::shared_ptr<SampleBuffer> decoded,
                                                  double rate,
                                                  const std::atomic<bool> *cancel = nullptr) const;
    /// activate()'s conversion of the current sample at `rate`, when its source is at another:
    /// the file at the saved path decoded again and converted, or, if it has gone, changed or
    /// can't be read, the old source kept and the status Missing, Changed or Error. When the
    /// source is at `rate` already, only checks the file, for a status that said it wasn't at
    /// hand. Either way, a file that is the sample's makes such a status Loaded.
    void convertNow(double rate) noexcept;
    /// The manifest entry's file searched for (through SearchCache) and decoded, and the status
    /// set: Loaded, Missing, Changed or Error. At `rate` 0 (inactive) the decoded buffer waits
    /// for activate() (restorePending_) and the result is nullptr; otherwise, the source at
    /// `rate`. A file found away from the saved path updates the entry and marks the state dirty.
    std::shared_ptr<const PreparedSource> findAndLoad(double rate);
    /// The status for the manifest's file not at hand: Changed when `read` (the file at `file`,
    /// the saved path, was read and isn't the sample's); otherwise Error when a file is there
    /// (it couldn't be read), and Missing when none is.
    void setUnavailable(const std::string &fileName, const std::filesystem::path &file, bool read);
    /// Whether the status says the manifest's file wasn't at hand: Missing, Changed, or the Error
    /// for a file that can't be read.
    bool fileUnavailable() const noexcept;
    void setLoadedStatus(std::string message);
    void setError(std::string fileName, std::string message);
    /// setError with `error` described, for a noexcept function's handler: if even that fails
    /// (memory), the status is Error with a short message that doesn't allocate.
    void setErrorNoThrow(const std::exception_ptr &error, const std::string &fileName) noexcept;
    /// A load or relink of `failedName` failed: Error, under the manifest entry's name when there
    /// is one (its sample plays on), else `failedName`.
    void setRequestError(const std::string &failedName, std::string message);
    /// setRequestError with `error` described, as setErrorNoThrow.
    void setRequestErrorNoThrow(const std::exception_ptr &error,
                                const std::string &failedName) noexcept;
    core::MachineSettings readSettings() const;

    const std::filesystem::path settingsFile_;
    const Callbacks callbacks_;
    std::function<void(const std::shared_ptr<const PreparedSource> &)> preparedObserver_;

    // Main thread.
    std::optional<core::MediaEntry> entry_;
    std::optional<std::string> newerMedia_; // a newer build's manifest, when there's no entry_
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
    std::atomic<bool> cancel_{false}; // set while stopWorker(true) waits: the job stops part way
    std::thread worker_;              // main thread only starts and joins it
};
} // namespace gg::plugin

#endif // GG_PLUGIN_SAMPLELOADER_H
