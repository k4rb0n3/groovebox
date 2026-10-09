#pragma once

#include "LaneData.h"
#include <unordered_map>

namespace cg
{
// What Groovebox "hears" in a sample.
struct Features
{
    bool  valid     = false;
    float durSec    = 0.0f;  // full file length
    float tailSec   = 0.0f;  // time from the attack until it drops 40 dB below its peak
    float attackMs  = 0.0f;  // 10% to 90% of peak
    float brightHz  = 0.0f;  // brightness estimate (zero-crossing based)
    float lowRatio  = 0.0f;  // share of energy below 150 Hz
    float highRatio = 0.0f;  // share of energy above 5 kHz
    float loudDb    = -100.0f;
    float pitchHz   = 0.0f;  // 0 = no clear pitch
};

Features analyzeBuffer (const float* mono, int numSamples, double sampleRate, double fileDurationSec);

struct Fit
{
    int score = -1;      // 0..100, -1 = not analyzed yet
    juce::String reason; // short human explanation
};

Fit fitScore (const Features&, Role role, int genre);
juce::String noteName (float hz);

// Analyzes files on a background thread and caches the results in
// Documents/Groovebox/analysis-cache.tsv so a library is only analyzed once.
class SampleAnalyzer : private juce::Thread
{
public:
    SampleAnalyzer();
    ~SampleAnalyzer() override;

    // Queue files for analysis; `first` (if any) jumps the queue.
    void enqueue (const juce::Array<juce::File>& files, const juce::File& first = {});
    bool get (const juce::File&, Features&) const;
    int  pending() const;

    std::atomic<int> version { 0 }; // bumps whenever new results arrive

private:
    void run() override;
    void loadCache();
    void saveCache();
    static juce::File cacheFile();

    struct Entry { Features f; juce::int64 size = 0, mtime = 0; };

    mutable juce::CriticalSection cs;
    std::vector<juce::File> queue;
    std::unordered_map<std::string, Entry> results;
    bool cacheLoaded = false, dirty = false;
    juce::AudioFormatManager fm;
};

} // namespace cg
