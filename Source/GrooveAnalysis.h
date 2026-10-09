#pragma once

#include "LaneData.h"

namespace cg
{
// Listens to a reference recording and extracts its groove: tempo, bar grid, and per 16th
// how often, how early/late and how loud lows, mids and highs play.
//   forcedBpm: 0 = detect automatically.
//   hintBpm:   used to pick between half/double tempo when detecting (e.g. FL's tempo).
Groove analyzeGroove (const float* mono, int numSamples, double sampleRate,
                      double forcedBpm, double hintBpm, const juce::String& name);

// Reads up to 90 s of an audio file and analyzes it.
Groove analyzeGrooveFile (juce::AudioFormatManager& formats, const juce::File& file,
                          double forcedBpm, double hintBpm);

// One-line human summary ("124 BPM, 16 bars, hats +28% swing...").
juce::String describeGroove (const Groove&);
} // namespace cg
