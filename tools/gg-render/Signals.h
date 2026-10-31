// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_SIGNALS_H
#define GG_RENDER_SIGNALS_H

#include <cstdint>
#include <memory>
#include <string>

#include "RenderSpec.h"
#include "gg/metrics/AudioData.h"
#include "gg/source/PreparedSource.h"

namespace gg::render
{
/// round(seconds · sampleRate) for finite arguments >= 0, rounding halves up. Plain IEEE
/// arithmetic (no libm), so the frame count, which reaches the grain-event log through the
/// source length, is the same on every platform.
int64_t framesFor(double seconds, double sampleRate) noexcept;

/// The source a render reads. Synthesized sources are mono, at rate r = source.sampleRate if it
/// is set and `sampleRate` (the render's rate) otherwise, and framesFor(seconds, r) frames long;
/// sample n is at time t = n / r:
/// - sine: amplitude · sin(2π · frequencyHz · t);
/// - sweep: exponential from startHz to endHz over `seconds`, i.e. instantaneous frequency
///   startHz · (endHz / startHz)^(t / seconds), starting at phase 0;
/// - noise: amplitude · gg::rng::bipolar(hash{seed, 0, 0, Field::SignalNoise, n}), white and
///   uniform in [-amplitude, amplitude).
///
/// Sine and sweep use libm, so their last bits may differ between platforms; they are audio,
/// which goldens compare by metrics with tolerances. Noise is bit-identical everywhere.
///
/// A file source is loaded as is, through gg::metrics::readAudioFile (M0's reader, so the
/// sample-mode goldens don't move), with its own channels and sample rate (`sampleRate` and
/// `seed` are unused); throws BadInput if it can't be read.
gg::metrics::AudioData makeSignal(const SourceSpec &source, double sampleRate, uint64_t seed);

/// An instrument's sample, ready for Processor::setSourceNow, at the render's rate `sampleRate`:
/// - a file is loaded with gg::media::loadSample (default MachineSettings) and made a source with
///   gg::media::prepare at `sampleRate`, as the plugin's loader does: converted with r8brain
///   when its rate differs, mono or stereo as the file is;
/// - a built-in signal is makeSignal(sample, sampleRate, seed), mono, prepared the same way.
/// Throws BadInput if the file can't be read or decoded, and std::invalid_argument for a sample
/// with a sampleRate of its own, which parseRenderSpec never produces.
std::shared_ptr<const PreparedSource> makeSample(const SourceSpec &sample, double sampleRate,
                                                 uint64_t seed);

/// The length of an FX input's fades, in seconds (ruling M1-v).
inline constexpr double kInputFadeSeconds = 0.01;
/// The length of an FX input's fades, in frames: kInputFadeSeconds × sampleRate rounded
/// (framesFor), at least 1.
int64_t inputFadeFrames(double sampleRate) noexcept;

/// The raised cosine 0.5 − 0.5·cos(πx) = sin²(πx/2) for x in [0, 1]: 0 at 0, 1 at 1, with zero
/// slope at both ends. sin comes from its Taylor series through θ^19, in plain IEEE arithmetic
/// (no libm), so the value is the same on every platform. On [0, π/2] the series' error is below
/// (π/2)^21 / 21! < 3e-16, so the result is within 1e-15 of the exact raised cosine.
double raisedCosine(double x) noexcept;

/// An FX render's input at the render's rate, faded in and out (below).
///
/// A built-in input is `channels` channels of makeSignal(input, sampleRate, seed). Channel 0 is
/// that signal. A second channel differs from it the way a stereo source would, except for the
/// sweep:
/// - noise: the noise of seed + 1, independent of channel 0;
/// - sine: frequencyHz × 1.5, a fifth up, at the same amplitude;
/// - sweep: the same sweep.
///
/// A file input (ruling M1-aa) is loaded and converted to `sampleRate` as makeSample loads a
/// file, with gg-media, and keeps the file's channels, mono or stereo, whatever `channels` says.
///
/// The fades (ruling M1-v): with L = inputFadeFrames(sampleRate) and N frames, sample n of every
/// channel is multiplied by raisedCosine(n / L) for n < L and by raisedCosine((N − 1 − n) / L)
/// for N − 1 − n < L (by both if an input is shorter than 2L), computed in double and rounded to
/// float. So the first and last samples are exactly 0 and every sample in between is unchanged:
/// the input starts and stops without the click a hard edge would put in the dry signal and in
/// the grains that replay it. Sample-mode sources (makeSignal) have no fades.
///
/// Throws BadInput if a file input can't be read or decoded, and std::invalid_argument for what
/// parseRenderSpec never produces: channels other than 1 or 2, or an input with a sampleRate of
/// its own.
gg::metrics::AudioData makeInput(const SourceSpec &input, uint32_t channels, double sampleRate,
                                 uint64_t seed);

/// The test media the golden cases read, which the build generates rather than commits (spec
/// §6.9), as 32-bit float WAVs:
/// - kToneMedium: stereo, 44.1 kHz, 4 s; a 50 Hz to 8 kHz exponential sweep on the left and a
///   220 Hz sine on the right (makeSignal's), each with a -6 dBFS peak level;
/// - kLoopMedium: mono, 48 kHz, 4 s; a 30 ms burst of seed 7's noise every 250 ms, decaying
///   exponentially to -60 dB at its end, silent in between, scaled to a -3 dBFS peak.
inline constexpr const char *kToneMedium = "tone-44k1.wav";
inline constexpr const char *kLoopMedium = "loop-48k.wav";
/// M2 sub-project 2's media (spec §6.10):
/// - kSineMedium: mono, 48 kHz, 2 s; a 330 Hz sine at amplitude 0.5;
/// - kNoiseMedium: mono, 48 kHz, 2 s; seed 3's white noise at amplitude 0.5;
/// - kFrozenRingState: a GGST file `gg-render` makes from a built-in FX spec (writeTestMedia): a
///   sweep over a 20 s ring on layer A, frozen at 18 s, with splices 12 s and 6 s behind the
///   head at 19 s, saved at 20 s (the ring embedded, its two marks 6 s apart);
/// - kFourSamplesState: a GGST file from a built-in instrument spec whose four layers, all on,
///   read kToneMedium, kLoopMedium, kSineMedium and kNoiseMedium, referenced by content path
///   relative to the media directory (no machine path in it).
inline constexpr const char *kSineMedium = "sine-330.wav";
inline constexpr const char *kNoiseMedium = "noise-2s.wav";
inline constexpr const char *kFrozenRingState = "frozen-ring.ggst";
inline constexpr const char *kFourSamplesState = "four-samples.ggst";

/// Writes the test media and the two state fixtures into `dir` (UTF-8), creating it if needed,
/// replacing files of the same names (gg-render --write-media). The states are rendered here, by
/// render() with --save-state's machinery, from the media just written, so they hold the same
/// bytes on every run on one machine. Returns false, with `error` naming the path, if the directory
/// can't be created or a file can't be written.
bool writeTestMedia(const std::string &dir, std::string &error);
} // namespace gg::render

#endif // GG_RENDER_SIGNALS_H
