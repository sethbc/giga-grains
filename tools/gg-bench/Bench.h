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
//      roundOrder(configurations, r): a fixed order rotated by r, so drift over a round (thermal
//      state, a noisy neighbour on a VM) doesn't land on the same configurations every round.
// Each configuration keeps its own state from one round to the next.
//
// Time is wall-clock (std::chrono::steady_clock). gg-bench's main() asks for a performance core
// before calling run(); the library itself doesn't touch the thread's scheduling.

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

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

struct Report
{
    Options options;
    std::string machine, compiler;
    std::vector<CaseResult> results;
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

/// Throws std::invalid_argument for an unknown case, a run length that isn't positive and
/// finite, fewer than 1 round or 0 grains.
void validate(const Options &options);

/// Times the cases options.cases names (all of them when it's empty), each at block sizes 32, 64
/// and 256. The results are in caseNames() order, then block size order. Calls validate() first,
/// so it throws before timing anything.
Report run(const Options &options, const Progress &progress = {});

/// The report as JSON schema 2: schemaVersion, machine, compiler, commit, runSeconds, rounds,
/// results (each CaseResult's fields, with nsPerGrainSample as "runs") and ratios (empty until
/// the engine cases arrive).
std::string toJson(const Report &report);

/// The order round `round` runs `configurations` configurations in: 0, 1, ..., rotated left by
/// `round`.
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
