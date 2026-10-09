#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <memory>
#include <limits>
#include <algorithm>
#include <cmath>

namespace cg
{
constexpr int kMaxLanes   = 32;
constexpr int kMaxSteps   = 1024; // 64 bars of 1/16
constexpr int kViewSteps  = 64;   // steps shown at once (one page)
constexpr int kNumOuts    = 32; // one stereo output per lane; output 1 is also Main
constexpr int kBaseNote   = 36; // C1 triggers lane 1, C#1 lane 2, ...

struct Step
{
    bool  on      = false;
    float vel     = 0.8f;  // 0..1
    float prob    = 1.0f;  // 0..1, chance the step plays each time around
    float nudge   = 0.0f;  // -0.5..0.5 of a step, micro timing
    int   ratchet = 1;     // 1..4 hits inside the step
    float pitch   = 0.0f;  // -12..12 semitones on top of the lane pitch
    float pan     = 0.0f;  // -1..1 on top of the lane pan
    float cutoff  = 1.0f;  // 0..1, 1 = lane cutoff, lower darkens (up to 6 octaves)
    float start   = 0.0f;  // 0..1 sample start offset (of the first 50% of the sample)
};

enum class FilterType { Off, LowPass, HighPass, BandPass };
enum class LaneMode { Sample, FLChannel };

enum class Role
{
    Kick, Snare, Clap, ClosedHat, OpenHat, Ride, Cymbal, Shaker,
    Perc, Tom, Rim, Click, Texture, FX, Bass, Other, NumRoles
};

constexpr int kNumRoles = (int) Role::NumRoles;

inline const char* roleName (Role r)
{
    static const char* names[] = { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Ride", "Cymbal", "Shaker",
                                   "Perc", "Tom", "Rim", "Click", "Texture", "FX", "Bass", "Other" };
    return names[juce::jlimit (0, kNumRoles - 1, (int) r)];
}

inline juce::Colour roleColour (Role r)
{
    static const juce::uint32 cols[] = { 0xffe8664a, 0xffe8a24a, 0xffe8c94a, 0xff9fd36a, 0xff6ad38e, 0xff6ad3c8,
                                         0xff6ab4d3, 0xff6a8fd3, 0xff8f6ad3, 0xffc06ad3, 0xffd36aa8, 0xffd36a6a,
                                         0xffa0a0a0, 0xffc8b48c, 0xffe87a9a, 0xff8c8c8c };
    return juce::Colour (cols[juce::jlimit (0, kNumRoles - 1, (int) r)]);
}

struct Rate { const char* name; double beats; };

inline const std::array<Rate, 9>& rates()
{
    static const std::array<Rate, 9> r {{ { "1/4", 1.0 }, { "1/8D", 0.75 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 },
                                          { "1/16D", 0.375 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 },
                                          { "1/32", 0.125 }, { "1/32T", 1.0 / 12.0 } }};
    return r;
}

constexpr int kRate16 = 5;

struct SampleData
{
    juce::AudioBuffer<float> buffer; // always 2 channels
    double sampleRate = 44100.0;
    juce::File file;
};

using SamplePtr = std::shared_ptr<const SampleData>;

struct Lane
{
    Role role = Role::Other;
    juce::String name;

    int length = 16;      // 1..64
    int rate   = kRate16; // index into rates()
    std::array<Step, kMaxSteps> steps {};

    // Level, pan, pitch and cutoff live in automatable parameters (see the processor).
    float decay    = 1.0f;  // 0..1, 1 = full sample
    float attack   = 0.0f;  // 0..1 -> 0..200 ms fade-in
    float reso     = 0.2f;  // 0..1 filter resonance
    FilterType filter = FilterType::Off;
    bool  reverse  = false;
    LaneMode mode  = LaneMode::Sample;
    int   midiChannel = 1;  // 1..16 when mode == FLChannel
    int   midiNote    = 60; // C5 in FL's naming
    bool  lockSample  = false; // randomizers and library scans leave the sample alone
    bool  lockPattern = false; // Generate and Mutate leave the pattern alone
    float swing    = 0.0f;  // 0..0.75
    float humanize = 0.15f; // 0..1
    bool  mute = false, solo = false;
    int   choke = 0;        // 0 = none, 1..4 groups
    int   out   = -1;       // -1 = own output (lane index), else output pair 0..31 (0 = Main)

    juce::File folder;
    juce::Array<juce::File> files;
    int fileIndex = -1;
    SamplePtr sample;
    juce::File pendingFile; // sample to select once a background folder scan finishes

    double stepBeats() const { return rates()[(size_t) juce::jlimit (0, (int) rates().size() - 1, rate)].beats; }
};

// Which steps of a lane are visible on a given page: { first step, count }.
// Lanes up to 64 steps always show whole; longer lanes are shown 64 steps per page.
inline std::pair<int, int> viewWindow (const Lane& lane, int page)
{
    const int len = juce::jlimit (1, kMaxSteps, lane.length);
    if (len <= kViewSteps) return { 0, len };
    const int pages = (len + kViewSteps - 1) / kViewSteps;
    const int p = ((page % pages) + pages) % pages;
    const int start = p * kViewSteps;
    return { start, juce::jmin (kViewSteps, len - start) };
}

// Deterministic 0..1 random from integers, so playback and MIDI export agree
// and an event never fires twice across block boundaries.
inline float hash01 (juce::uint32 seed, int lane, juce::int64 k, int salt)
{
    juce::uint64 x = (juce::uint64) seed * 0x9E3779B97F4A7C15ull
                   ^ ((juce::uint64) (lane + 1) * 0xBF58476D1CE4E5B9ull)
                   ^ ((juce::uint64) k * 0x94D049BB133111EBull)
                   ^ ((juce::uint64) (salt + 7) * 0x2545F4914F6CDD1Dull);
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (float) ((x >> 40) & 0xFFFFFF) / (float) 0x1000000;
}

// Calls fn(ppqTime, velocity, step) for every hit of this lane in [from, to).
template <typename Fn>
void collectEvents (const Lane& lane, int laneIndex, juce::uint32 seed, double from, double to,
                    double bpm, float swingScale, float humanizeScale, Fn&& fn,
                    juce::int64 clipSteps = -1) // >= 0: emit steps 0..clipSteps-1 once each, ignoring the window
{
    const double sb  = lane.stepBeats();
    const int    len = juce::jlimit (1, kMaxSteps, lane.length);

    const bool clip  = clipSteps >= 0;
    const auto kFrom = clip ? (juce::int64) 0 : (juce::int64) std::floor (from / sb) - 2;
    const auto kTo   = clip ? clipSteps - 1 : (juce::int64) std::ceil (to / sb) + 2;

    const float hum = juce::jlimit (0.0f, 2.0f, lane.humanize * humanizeScale);

    for (auto k = juce::jmax ((juce::int64) 0, kFrom); k <= kTo; ++k)
    {
        const auto& st = lane.steps[(size_t) (k % len)];
        if (! st.on)
            continue;

        if (st.prob < 0.999f && hash01 (seed, laneIndex, k, 1) >= st.prob)
            continue;

        double t = (double) k * sb + (double) st.nudge * sb;

        if ((k & 1) == 1)
            t += (double) (lane.swing * swingScale) * 0.5 * sb;

        float vel = st.vel;

        if (hum > 0.0f)
        {
            const double jitterSec = ((double) hash01 (seed, laneIndex, k, 2) * 2.0 - 1.0) * hum * 0.010;
            t += jitterSec * bpm / 60.0;
            vel *= 1.0f + (hash01 (seed, laneIndex, k, 3) * 2.0f - 1.0f) * hum * 0.22f;
        }

        vel = juce::jlimit (0.02f, 1.0f, vel);

        const int rat = juce::jlimit (1, 4, st.ratchet);

        for (int r = 0; r < rat; ++r)
        {
            const double tr = t + (double) r * sb / rat;
            const float  vr = r == 0 ? vel : vel * (0.8f - 0.08f * (float) r);

            if (clip || (tr >= from && tr < to))
                fn (tr, vr, st);
        }
    }
}

} // namespace cg
