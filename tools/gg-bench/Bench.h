// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_BENCH_BENCH_H
#define GG_BENCH_BENCH_H

// gg-bench's timing library, shared by the gg-bench executable and gg-tests. It times every
// configuration, a case at one block size, in interleaved rounds (carry-over B-I2, [133]) and
// reports every run with its median, min and IQR, as JSON schema 2 ([132]).
//
// A run of the benchmark:
//   1. builds every configuration once, and warms each up once;
//   2. then, for round r in [0, rounds), times every configuration once for runSeconds, in
//      roundOrder(configurations, r): a fixed order rotated by r, and reversed in odd rounds, so
//      drift over a round (thermal state, a noisy neighbour on a VM) doesn't land on the same
//      configurations every round, and each configuration follows different ones.
// Each configuration keeps its own state from one round to the next.
//
// Time is wall-clock (std::chrono::steady_clock). gg-bench's main() asks for a performance core
// before calling run(); the library itself doesn't touch the thread's scheduling.
//
// The cases (spec §5):
// - the M0 prototype kernels (Kernels.h), each over the M0 workload: Options::grains grains of
//   100 ms over 60 s of stereo noise;
// - `reference`: renderHermiteSimd on that workload, frozen as the CPU gate's yardstick;
// - the engine cases, each a gg::Engine at 48 kHz rendering one Cloud stream through
//   Engine::process: engine-sample (X, the headline), engine-sample-eco (X at the Eco tier),
//   engine-live, engine-live-long (a 300 s ring, report-only) and engine-full-pool;
// - the four-layer engine cases (M2 layers spec §6.12), report-only beside X: engine-layers,
//   four such engines (layers 0 to 3) sharing X's grains, 500 each, rendered through a
//   SerialRunner and summed in layer order; and engine-layers-full, the same with all of the
//   grains in every layer (8,000 in all by default).
// Every engine case's ratio, ns per grain-sample over reference's in the same round, cancels the
// machine's speed (ruling 3), though not its microarchitecture: on CI the same code's ratios
// differ by up to ~20% between the CPU models GitHub assigns a leg. A baseline file therefore
// records each ratio per CI leg and per CPU model (the `machine` string), and compare() checks a
// report only against its own model's entry, report-only through M1.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace gg::bench
{
struct Options
{
    double runSeconds{2.0};           // --seconds; --quick = 0.2
    bool quick{false};                // --quick: caps each engine case's warm-up (Bench.cpp)
    int rounds{5};                    // --rounds
    uint32_t grains{2000};            // --grains
    std::vector<std::string> cases{}; // --cases a,b; empty = all
    std::string commit{"unknown"};    // --commit
};

struct CaseResult
{
    std::string name;
    std::string kind; // "kernel" | "engine"
    uint32_t blockSize{0};
    double grains{0.0};                     // nominal (kernel) or mean active (engine)
    std::vector<double> nsPerGrainSample;   // one per round, in round order
    double median{0.0}, min{0.0}, iqr{0.0}; // of nsPerGrainSample
    double cpuPctOneCoreAt48k{0.0};         // from the median
};

/// An engine case's cost relative to the reference kernel, at one block size: runs[r] is the
/// case's ns per grain-sample in round r over reference's in the same round.
struct Ratio
{
    std::string name;
    uint32_t blockSize{0};
    std::vector<double> runs{}; // one per round, in round order
    double median{0.0}, iqr{0.0};
};

struct Report
{
    Options options;
    std::string machine, compiler;
    std::vector<CaseResult> results;
    std::vector<Ratio> ratios; // computeRatios(results)
};

/// One ratio checked against a baseline: regressed when ratio > limit = the baseline's ratio
/// × (1 + its tolerance). A ratio the baseline doesn't have has a NaN limit and isn't regressed.
/// A baseline entry the report doesn't have has a NaN ratio (see Comparison::missing).
struct Verdict
{
    std::string name;
    uint32_t blockSize{0};
    double ratio{0.0}, limit{0.0};
    bool regressed{false};
};

/// A report checked against a baseline file.
struct Comparison
{
    /// The baseline has an entry for the report's machine. When it doesn't, there is no verdict:
    /// the other fields are empty.
    bool found{false};
    std::string commit;            // the entry's
    uint32_t runs{0};              // how many runs the entry was made from
    std::vector<Verdict> verdicts; // one per report ratio, in the report's order
    /// The entry's ratios the report doesn't have, in the entry's order, each with its limit and
    /// a NaN ratio. Each is a failure: the gate can't pass a ratio it didn't measure.
    std::vector<Verdict> missing;

    /// The verdicts that regressed plus the missing entries: the gate passes only at 0.
    std::size_t failures() const
    {
        std::size_t n = missing.size();
        for (const Verdict &v : verdicts)
        {
            n += v.regressed ? 1 : 0;
        }
        return n;
    }
};

/// One timed configuration: a case at one block size, with the state it keeps between rounds.
/// Each kind of case ("kernel", "engine") derives from it.
class Configuration
{
  public:
    Configuration(std::string name, std::string kind, uint32_t blockSize)
        : name_(std::move(name)), kind_(std::move(kind)), blockSize_(blockSize)
    {
    }
    virtual ~Configuration() = default;
    Configuration(const Configuration &) = delete;
    Configuration &operator=(const Configuration &) = delete;

    /// Called once, before the first round. How long it runs is the configuration's own choice.
    virtual void warmUp() = 0;

    /// Renders blocks for at least `seconds` of wall-clock time; returns nanoseconds per
    /// stereo grain-sample.
    virtual double timeRun(double seconds) = 0;

    /// The grains a block renders: the nominal count, or, for a case whose count varies, the
    /// mean over the timed runs so far.
    virtual double grains() const = 0;

    const std::string &name() const { return name_; }
    const std::string &kind() const { return kind_; }
    uint32_t blockSize() const { return blockSize_; }

  private:
    std::string name_;
    std::string kind_;
    uint32_t blockSize_;
};

/// The names of every case, in the order run() reports them.
std::vector<std::string> caseNames();

/// Called after each round with the round just finished (1-based) and the round count.
using Progress = std::function<void(int round, int rounds)>;

/// Throws std::invalid_argument for an unknown case, a run length that isn't in (0, 3600]
/// seconds, fewer than 1 round or 0 grains.
void validate(const Options &options);

/// Times the cases options.cases names (all of them when it's empty), each at block sizes 32, 64
/// and 256. The results are in caseNames() order, then block size order; the ratios are
/// computeRatios()'s. Calls validate() first, so it throws before timing anything.
Report run(const Options &options, const Progress &progress = {});

/// A ratio for every engine result whose block size has a `reference` result, in the results'
/// order: runs[r] = engine.nsPerGrainSample[r] / reference.nsPerGrainSample[r], with their
/// median and IQR. Throws std::invalid_argument if the two have different run counts.
std::vector<Ratio> computeRatios(const Report &report);

/// The report as JSON schema 2: schemaVersion, machine, compiler, commit, runSeconds, rounds,
/// results (each CaseResult's fields, with nsPerGrainSample as "runs") and ratios (each Ratio's
/// fields). Invalid UTF-8 in a string (the commit, say) is replaced, never an error.
std::string toJson(const Report &report);

/// Throws std::invalid_argument unless `baseline` is a baseline file of this shape:
///     {"schemaVersion": 2, "leg": "<preset>",
///      "machines": [{"machine": "<machine()>", "commit": "<sha>", "runs": N,
///                    "ratios": [{"case", "blockSize", "ratio", "tolerance"}, ...]}, ...]}
/// with at most one entry per machine, runs >= 1, at least one ratio per entry, each (case,
/// blockSize) at most once in an entry, and a positive finite ratio and a finite tolerance >= 0
/// in every ratio.
void checkBaseline(const nlohmann::json &baseline);

/// A report's ratios checked against the baseline's entry for report.machine, in the report's
/// order, and the entry's ratios the report lacks (missing). Each limit is the entry's ratio
/// × (1 + its tolerance). With no entry for report.machine, found is false and there are no
/// verdicts. Throws std::invalid_argument for a baseline checkBaseline() rejects (it checks the
/// whole file first), and when there is an entry but the report has no ratios: a verdict over
/// nothing is an error, never "0 of 0".
Comparison compare(const Report &report, const nlohmann::json &baseline);

/// A baseline for `leg` from parsed gg-bench JSON reports (schema 2): one entry per machine, in
/// the order the machines first appear. A machine's runs must all have the same (case, block
/// size) ratios. Each ratio is the median over that machine's runs of each run's median ratio,
/// and its tolerance max(0.10, 2 × (max − min) / median), so 0.10 for a single run. The commit is
/// the runs' commit, or their distinct commits joined by commas. Throws std::invalid_argument for
/// no runs, a run that isn't schema 2, a ratio whose median isn't positive and finite, a machine
/// whose runs' ratios differ, or a result checkBaseline() rejects (a tolerance that overflows to
/// infinity, say): it never returns a baseline compare() would refuse.
nlohmann::json makeBaseline(const std::string &leg, const std::vector<nlohmann::json> &runs);

/// `old` with `made`'s entries merged in, so a CPU model can be added to, or remade in, a
/// baseline without the runs the other entries came from (CI's artifacts expire). Each of old's
/// entries stays where it is, replaced by made's entry for the same machine when there is one;
/// made's entries for machines old lacks follow, in made's order. Throws std::invalid_argument
/// if either fails checkBaseline() or their legs differ.
nlohmann::json mergeBaseline(const nlohmann::json &old, const nlohmann::json &made);

/// How many grains an engine case admitted at each interpolation tier (ruling 2).
struct TierCounts
{
    uint64_t eco{0};
    uint64_t normal{0};
    /// Each layer's engine's activeGrains() after the warm-up, in layer order: one entry for a
    /// one-layer case, four for engine-layers and engine-layers-full.
    std::vector<uint32_t> activeGrains{};
};

/// Builds engine case `engineCase` at `blockSize` with `grains` grains exactly as run() does,
/// warms it up as run() does, and counts the tier of every grain it admitted meanwhile, through
/// its engines' event sinks (every layer's, for a four-layer case): each admitted grain's spec
/// carries the tier the pool latches for it. It proves which tier a case runs at
/// (engine-sample-eco: Eco; the others: Normal), and how a layered case's grains are spread over
/// its layers (TierCounts::activeGrains), without timing anything. Throws std::invalid_argument for
/// a name that isn't an engine case, a block size run() doesn't use (32, 64 and 256) or 0 grains.
TierCounts admittedTiers(const std::string &engineCase, uint32_t grains, uint32_t blockSize);

/// An engine case's setup as run() uses it, before any warm-up: its live ring's length in
/// seconds (0 for Sample mode), whether it reads the live ring, the reads' position and spray in
/// seconds, and its warm-up in seconds of audio (capped under --quick). Throws
/// std::invalid_argument for a name that isn't an engine case or 0 grains.
struct EngineCaseSettings
{
    double liveSeconds{0.0};
    bool live{false};
    double position{0.0};
    double spray{0.0};
    double warmUpSeconds{0.0};
};
EngineCaseSettings engineCaseSettings(const std::string &engineCase, uint32_t grains, bool quick);

/// The order round `round` (r) runs `configurations` (n) configurations in. Even rounds run
/// 0, 1, ..., n - 1 rotated left by r: order[i] = (i + r) mod n. Odd rounds run that order's
/// reverse, rotated: order[i] = (r + n - 1 - i) mod n. Each configuration's predecessor thus
/// alternates between its two neighbours, and for n >= 2 the last of one round is never the first
/// of the next, so no configuration runs twice back to back across a round seam.
std::vector<uint32_t> roundOrder(uint32_t configurations, int round);

/// The q-th quantile (0..1, clamped) of v, interpolating linearly between order statistics; NaN
/// when v is empty.
double quantile(std::vector<double> v, double q);

/// The CPU, plus the architecture this binary was built for (a universal macOS build has two).
std::string machine();

/// The compiler gg-bench was built with.
std::string compiler();

// ---- gg-bench --space: Space's cost (sub-project 4, report-only) ----
//
// It times Space's per-sample stage, Diffuser::process then SpaceReverb::processSample, on
// stereo noise at 48 kHz, active, with Diffusion 1, Reverb Amount 1, Size 0.5, Decay 1.0 (log2
// seconds, finite), Damping 0.3, Modulation 0.25 and no pre-delay. After a warm-up of 2 s of
// audio, each round times one run of at least SpaceOptions::runSeconds and records its ns per
// stereo sample. There is one configuration, so there is no round order, no ratio and no
// baseline: sub-project 6 publishes the figure beside the grain engine's (INTENT §16).

struct SpaceOptions
{
    double runSeconds{2.0};        // --seconds; --quick = 0.2
    int rounds{5};                 // --rounds
    std::string commit{"unknown"}; // --commit
};

struct SpaceReport
{
    SpaceOptions options;
    std::string machine, compiler;
    std::vector<double> nsPerSample{};      // one per round, in round order
    double median{0.0}, min{0.0}, iqr{0.0}; // of nsPerSample
    double cpuPctOneCoreAt48k{0.0};         // from the median
};

/// Throws std::invalid_argument for a run length that isn't in (0, 3600] seconds or fewer than
/// 1 round.
void validate(const SpaceOptions &options);

/// Times Space as above. Calls validate() first, so it throws before timing anything.
SpaceReport runSpace(const SpaceOptions &options, const Progress &progress = {});

/// The report as its own JSON document, apart from schema 2: {"benchmark": "space", machine,
/// compiler, commit, sampleRate, runSeconds, rounds, the settings, "runs" (nsPerSample),
/// "nsPerSample" (the median), "min", "iqr", "cpuPctOneCoreAt48k"}.
std::string toJson(const SpaceReport &report);

/// gg-bench --space: runs runSpace(options), puts the text gg-bench prints in `out` and, when
/// jsonPath isn't empty, writes toJson() there. Returns gg-bench's exit status: 0, or 1 if it
/// couldn't write the JSON. Throws std::invalid_argument for options validate() rejects.
int spaceMain(const SpaceOptions &options, const std::string &jsonPath, std::string &out,
              const Progress &progress = {});
} // namespace gg::bench

#endif // GG_BENCH_BENCH_H
