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
//   engine-live and engine-full-pool.
// Every engine case's ratio, ns per grain-sample over reference's in the same round, cancels the
// machine's speed (ruling 3). A baseline file records each ratio per CI leg; compare() checks a
// report against it, report-only through M1.

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
    double runSeconds{2.0};         // --seconds; --quick = 0.2
    int rounds{5};                  // --rounds
    uint32_t grains{2000};          // --grains
    std::vector<std::string> cases; // --cases a,b; empty = all
    std::string commit{"unknown"};  // --commit
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
    std::vector<double> runs; // one per round, in round order
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
struct Verdict
{
    std::string name;
    uint32_t blockSize{0};
    double ratio{0.0}, limit{0.0};
    bool regressed{false};
};

/// One timed configuration: a case at one block size, with the state it keeps between rounds.
/// Each kind of case ("kernel", and later "engine") derives from it.
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

/// A report's ratios checked against a baseline, in the report's order. The baseline is
///     {"schemaVersion": 1, "leg": "<preset>", "commit": "<sha>",
///      "ratios": [{"case", "blockSize", "ratio", "tolerance"}, ...]}
/// and each limit is ratio × (1 + tolerance). Throws std::invalid_argument if the baseline isn't
/// that shape, with a positive finite ratio and a finite tolerance >= 0 in every entry, even when
/// the report has no ratios.
std::vector<Verdict> compare(const Report &report, const nlohmann::json &baseline);

/// A baseline for `leg` from parsed gg-bench JSON reports (schema 2), which must all have the
/// same (case, block size) ratios. Each entry's ratio is the median over the runs of each run's
/// median ratio, and its tolerance max(0.05, 2 × (max − min) / median). The commit is the runs'
/// commit, or their distinct commits joined by commas. Throws std::invalid_argument for no runs,
/// a run that isn't schema 2 or runs whose ratios differ.
nlohmann::json makeBaseline(const std::string &leg, const std::vector<nlohmann::json> &runs);

/// The order round `round` (r) runs `configurations` (n) configurations in. Even rounds run
/// 0, 1, ..., n - 1 rotated left by r: order[i] = (i + r) mod n. Odd rounds run that order's
/// reverse, rotated: order[i] = (r + n - 1 - i) mod n. Each configuration's predecessor thus
/// alternates between its two neighbours, and the last of one round is never the first of the
/// next, so no configuration runs twice back to back across a round seam.
std::vector<uint32_t> roundOrder(uint32_t configurations, int round);

/// The q-th quantile (0..1) of v, interpolating linearly between order statistics. v must not
/// be empty.
double quantile(std::vector<double> v, double q);

/// The CPU, plus the architecture this binary was built for (a universal macOS build has two).
std::string machine();

/// The compiler gg-bench was built with.
std::string compiler();
} // namespace gg::bench

#endif // GG_BENCH_BENCH_H
