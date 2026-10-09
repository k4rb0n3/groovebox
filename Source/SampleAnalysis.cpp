#include "SampleAnalysis.h"
#include "Generator.h"

namespace cg
{
//==============================================================================
Features analyzeBuffer (const float* x, int n, double sr, double fileDurationSec)
{
    Features f;
    f.durSec = (float) fileDurationSec;
    if (x == nullptr || n < 16 || sr <= 0.0)
        return f;

    // Envelope in 1 ms windows (peak) and 10 ms windows (RMS).
    const int w1 = juce::jmax (1, (int) (sr * 0.001));
    const int w10 = juce::jmax (1, (int) (sr * 0.010));

    float peak = 0.0f;
    int peakIdx = 0;
    for (int i = 0; i < n; ++i)
        if (std::abs (x[i]) > peak) { peak = std::abs (x[i]); peakIdx = i; }

    if (peak < 1.0e-5f)
        return f; // silent file

    // Attack: first 1 ms window reaching 10% of peak, then 90%.
    int i10 = -1, i90 = -1;
    for (int start = 0; start < n && i90 < 0; start += w1)
    {
        float m = 0.0f;
        for (int i = start; i < juce::jmin (n, start + w1); ++i) m = juce::jmax (m, std::abs (x[i]));
        if (i10 < 0 && m >= 0.1f * peak) i10 = start;
        if (m >= 0.9f * peak) i90 = start;
    }
    if (i10 < 0) i10 = 0;
    if (i90 < 0) i90 = peakIdx;
    f.attackMs = (float) ((i90 - i10) * 1000.0 / sr);

    // Tail: last 10 ms window whose RMS is within 40 dB of the loudest window.
    float maxRms = 0.0f;
    std::vector<float> rms;
    for (int start = 0; start < n; start += w10)
    {
        double acc = 0.0;
        const int end = juce::jmin (n, start + w10);
        for (int i = start; i < end; ++i) acc += (double) x[i] * x[i];
        const float r = (float) std::sqrt (acc / juce::jmax (1, end - start));
        rms.push_back (r);
        maxRms = juce::jmax (maxRms, r);
    }
    int lastLoud = 0;
    for (int k = 0; k < (int) rms.size(); ++k)
        if (rms[(size_t) k] >= maxRms * 0.01f) lastLoud = k;
    f.tailSec = (float) (juce::jmax (0, (lastLoud + 1) * w10 - i10) / sr);
    if (lastLoud == (int) rms.size() - 1)
        f.tailSec = juce::jmax (f.tailSec, (float) fileDurationSec - (float) (i10 / sr)); // still sounding at the end

    // Band energy shares over the first second after the attack.
    const int a0 = i10, a1 = juce::jmin (n, i10 + (int) sr);
    {
        juce::IIRFilter lp, hp;
        lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 150.0));
        hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, juce::jmin (5000.0, sr * 0.45)));
        double eAll = 0.0, eLow = 0.0, eHigh = 0.0;
        for (int i = 0; i < a1; ++i)
        {
            const float l = lp.processSingleSampleRaw (x[i]);
            const float h = hp.processSingleSampleRaw (x[i]);
            if (i >= a0)
            {
                eAll += (double) x[i] * x[i];
                eLow += (double) l * l;
                eHigh += (double) h * h;
            }
        }
        if (eAll > 0.0)
        {
            f.lowRatio = (float) juce::jlimit (0.0, 1.0, eLow / eAll);
            f.highRatio = (float) juce::jlimit (0.0, 1.0, eHigh / eAll);
        }
    }

    // Brightness from zero-crossing rate over 300 ms after the attack.
    {
        const int b1 = juce::jmin (n, i10 + (int) (sr * 0.3));
        int crossings = 0;
        for (int i = i10 + 1; i < b1; ++i)
            if ((x[i - 1] >= 0.0f) != (x[i] >= 0.0f)) ++crossings;
        const int len = juce::jmax (1, b1 - i10 - 1);
        f.brightHz = (float) (crossings * sr / (2.0 * len));
    }

    // Loudness of the first 300 ms.
    {
        const int b1 = juce::jmin (n, i10 + (int) (sr * 0.3));
        double acc = 0.0;
        for (int i = i10; i < b1; ++i) acc += (double) x[i] * x[i];
        const double r = std::sqrt (acc / juce::jmax (1, b1 - i10));
        f.loudDb = (float) juce::Decibels::gainToDecibels (r, -100.0);
    }

    // Pitch: normalized autocorrelation 30 ms after the attack (skips the click of a kick).
    {
        // Kicks and toms sweep down in pitch; measure the settled body 80 ms in when the sample is long enough.
        const int late = i10 + (int) (sr * 0.08), early = i10 + (int) (sr * 0.03);
        const int s0 = juce::jmin (n - 1, (n - late > (int) (sr * 0.06)) ? late : early);
        const int N = juce::jmin (4096, n - s0);
        const int minLag = juce::jmax (2, (int) (sr / 1000.0));
        const int maxLag = juce::jmin (N / 2, (int) (sr / 30.0));
        if (N > 512 && maxLag > minLag + 2)
        {
            const float* seg = x + s0;
            double e0 = 0.0;
            for (int i = 0; i < N; ++i) e0 += (double) seg[i] * seg[i];
            if (e0 > 1.0e-8)
            {
                std::vector<double> ac ((size_t) (maxLag + 2), 0.0);
                for (int lag = minLag; lag <= maxLag + 1; ++lag)
                {
                    double acc = 0.0, e1 = 0.0, e2 = 0.0;
                    for (int i = 0; i + lag < N; ++i)
                    {
                        acc += (double) seg[i] * seg[i + lag];
                        e1 += (double) seg[i] * seg[i];
                        e2 += (double) seg[i + lag] * seg[i + lag];
                    }
                    ac[(size_t) lag] = acc / std::sqrt (juce::jmax (1.0e-12, e1 * e2));
                }

                // First strong peak (avoids octave errors), refined by parabolic interpolation.
                double best = 0.0;
                int bestLag = 0;
                for (int lag = minLag + 1; lag <= maxLag; ++lag)
                    if (ac[(size_t) lag] > ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1] && ac[(size_t) lag] > 0.75)
                    {
                        best = ac[(size_t) lag];
                        bestLag = lag;
                        break;
                    }

                if (bestLag > 0 && best > 0.75)
                {
                    const double a = ac[(size_t) bestLag - 1], b = ac[(size_t) bestLag], c = ac[(size_t) bestLag + 1];
                    const double denom = a - 2.0 * b + c;
                    const double shift = std::abs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0;
                    f.pitchHz = (float) (sr / ((double) bestLag + juce::jlimit (-0.5, 0.5, shift)));
                }
            }
        }
    }

    f.valid = true;
    return f;
}

//==============================================================================
namespace
{
enum Feat { Tail, Attack, Bright, Low, High, Dur, Pitch };

struct Crit
{
    Feat feat;
    float idealLo, idealHi, hardLo, hardHi, weight;
    bool logScale;
};

float value (const Features& f, Feat k)
{
    switch (k)
    {
        case Tail:   return f.tailSec;
        case Attack: return f.attackMs;
        case Bright: return f.brightHz;
        case Low:    return f.lowRatio;
        case High:   return f.highRatio;
        case Dur:    return f.durSec;
        case Pitch:  return f.pitchHz;
    }
    return 0.0f;
}

float penalty (const Crit& c, float v)
{
    auto tr = [&c] (float a) { return c.logScale ? std::log (juce::jmax (1.0e-4f, a)) : a; };
    const float x = tr (v);
    const float lo = tr (c.idealLo), hi = tr (c.idealHi), hlo = tr (c.hardLo), hhi = tr (c.hardHi);
    if (x >= lo && x <= hi) return 0.0f;
    if (x < lo) return juce::jlimit (0.0f, 1.0f, (lo - x) / juce::jmax (1.0e-6f, lo - hlo));
    return juce::jlimit (0.0f, 1.0f, (x - hi) / juce::jmax (1.0e-6f, hhi - hi));
}

enum class Family { Club, Deep, Techno, Experimental };

Family familyOf (int genre)
{
    switch (genre)
    {
        case Techno:                                  return Family::Techno;
        case DeepHouse: case DubTechno: case Minimal:
        case Ambient:                                 return Family::Deep;
        case IDM: case Free: case Hybrid:             return Family::Experimental;
        default:                                      return Family::Club;
    }
}

std::vector<Crit> profile (Role role, int genre)
{
    const auto fam = familyOf (genre);
    std::vector<Crit> c;

    switch (role)
    {
        case Role::Kick:
        {
            float tLo = 0.15f, tHi = 0.5f;
            if (fam == Family::Techno) { tLo = 0.25f; tHi = 0.9f; }
            if (fam == Family::Deep)   { tLo = 0.2f;  tHi = 0.8f; }
            c.push_back ({ Low, 0.45f, 1.0f, 0.1f, 1.0f, 3.0f, false });
            c.push_back ({ Tail, tLo, tHi, 0.05f, 2.5f, 2.0f, true });
            c.push_back ({ Attack, 0.0f, fam == Family::Deep ? 25.0f : 12.0f, 0.0f, 80.0f, 1.0f, false });
            c.push_back ({ Bright, 30.0f, 450.0f, 15.0f, 3000.0f, 1.0f, true });
            break;
        }
        case Role::Snare:
        case Role::Clap:
        {
            const float bLo = fam == Family::Deep ? 600.0f : 800.0f, bHi = fam == Family::Deep ? 3800.0f : 5500.0f;
            c.push_back ({ Low, 0.0f, 0.3f, 0.0f, 0.75f, 2.0f, false });
            c.push_back ({ Bright, bLo, bHi, 150.0f, 14000.0f, 2.0f, true });
            c.push_back ({ Tail, 0.08f, 0.5f, 0.02f, 1.8f, 2.0f, true });
            c.push_back ({ Attack, 0.0f, role == Role::Clap ? 30.0f : 15.0f, 0.0f, 90.0f, 1.0f, false });
            break;
        }
        case Role::ClosedHat:
        {
            const float tHi = (fam == Family::Club || fam == Family::Deep) ? 0.2f : 0.15f;
            const float bHi = fam == Family::Deep ? 11000.0f : 16000.0f;
            c.push_back ({ High, 0.35f, 1.0f, 0.05f, 1.0f, 3.0f, false });
            c.push_back ({ Tail, 0.015f, tHi, 0.004f, 0.7f, 3.0f, true });
            c.push_back ({ Bright, 3500.0f, bHi, 1200.0f, 22000.0f, 1.0f, true });
            c.push_back ({ Low, 0.0f, 0.1f, 0.0f, 0.5f, 1.0f, false });
            break;
        }
        case Role::OpenHat:
            c.push_back ({ High, 0.3f, 1.0f, 0.05f, 1.0f, 3.0f, false });
            c.push_back ({ Tail, 0.18f, 1.1f, 0.06f, 3.5f, 3.0f, true });
            c.push_back ({ Bright, 3500.0f, 16000.0f, 1200.0f, 22000.0f, 1.0f, true });
            break;
        case Role::Ride:
            c.push_back ({ High, 0.25f, 1.0f, 0.03f, 1.0f, 2.0f, false });
            c.push_back ({ Tail, 0.4f, 3.0f, 0.12f, 9.0f, 2.0f, true });
            c.push_back ({ Bright, 3000.0f, 14000.0f, 900.0f, 22000.0f, 1.0f, true });
            break;
        case Role::Cymbal:
            c.push_back ({ Tail, 0.8f, 6.0f, 0.25f, 20.0f, 3.0f, true });
            c.push_back ({ High, 0.25f, 1.0f, 0.03f, 1.0f, 2.0f, false });
            break;
        case Role::Shaker:
            c.push_back ({ High, 0.3f, 1.0f, 0.05f, 1.0f, 2.0f, false });
            c.push_back ({ Tail, 0.03f, 0.25f, 0.008f, 0.9f, 2.0f, true });
            c.push_back ({ Attack, 0.0f, 40.0f, 0.0f, 150.0f, 1.0f, false });
            break;
        case Role::Perc:
            c.push_back ({ Bright, 300.0f, 5000.0f, 70.0f, 16000.0f, 2.0f, true });
            c.push_back ({ Tail, 0.03f, 0.5f, 0.008f, 2.0f, 2.0f, true });
            c.push_back ({ Attack, 0.0f, 15.0f, 0.0f, 90.0f, 1.0f, false });
            break;
        case Role::Tom:
            c.push_back ({ Low, 0.15f, 0.85f, 0.0f, 1.0f, 1.5f, false });
            c.push_back ({ Bright, 70.0f, 1200.0f, 35.0f, 6000.0f, 2.0f, true });
            c.push_back ({ Tail, 0.15f, 0.9f, 0.05f, 3.0f, 2.0f, true });
            break;
        case Role::Rim:
            c.push_back ({ Tail, 0.01f, 0.12f, 0.003f, 0.6f, 2.0f, true });
            c.push_back ({ Bright, 700.0f, 6000.0f, 200.0f, 16000.0f, 2.0f, true });
            c.push_back ({ Attack, 0.0f, 5.0f, 0.0f, 40.0f, 1.0f, false });
            break;
        case Role::Click:
            c.push_back ({ Tail, 0.002f, 0.06f, 0.0005f, 0.5f, 3.0f, true });
            c.push_back ({ Attack, 0.0f, 3.0f, 0.0f, 30.0f, 1.0f, false });
            break;
        case Role::Texture:
            c.push_back ({ Dur, 1.0f, 60.0f, 0.25f, 900.0f, 3.0f, true });
            break;
        case Role::FX:
            c.push_back ({ Dur, 0.5f, 30.0f, 0.12f, 900.0f, 3.0f, true });
            break;
        case Role::Bass:
            c.push_back ({ Low, 0.4f, 1.0f, 0.1f, 1.0f, 2.0f, false });
            c.push_back ({ Pitch, 30.0f, 160.0f, 20.0f, 450.0f, 1.5f, true });
            break;
        case Role::Other:
        case Role::NumRoles:
            break;
    }

    // Experimental genres accept a much wider range of sounds.
    if (fam == Family::Experimental)
        for (auto& k : c)
        {
            if (k.logScale) { k.idealLo /= 1.8f; k.idealHi *= 1.8f; }
            else            { k.idealLo *= 0.6f; k.idealHi = juce::jmin (k.hardHi, k.idealHi * 1.5f + 0.05f); }
        }

    return c;
}

juce::String describe (const Features& f, Feat k)
{
    switch (k)
    {
        case Tail:   return "tail " + juce::String (juce::roundToInt (f.tailSec * 1000.0f)) + " ms";
        case Attack: return "attack " + juce::String (f.attackMs, 1) + " ms";
        case Bright: return "brightness ~" + (f.brightHz >= 1000.0f ? juce::String (f.brightHz / 1000.0f, 1) + " kHz"
                                                                      : juce::String (juce::roundToInt (f.brightHz)) + " Hz");
        case Low:    return "low end " + juce::String (juce::roundToInt (f.lowRatio * 100.0f)) + "%";
        case High:   return "top end " + juce::String (juce::roundToInt (f.highRatio * 100.0f)) + "%";
        case Dur:    return "length " + juce::String (f.durSec, 1) + " s";
        case Pitch:  return f.pitchHz > 0.0f ? "pitch " + noteName (f.pitchHz) : juce::String ("no clear pitch");
    }
    return {};
}

juce::String problem (const Crit& c, const Features& f)
{
    const float v = value (f, c.feat);
    const bool low = v < c.idealLo;
    switch (c.feat)
    {
        case Tail:   return (low ? "too short" : "too long") + juce::String (" (") + describe (f, Tail) + ")";
        case Attack: return "soft attack (" + describe (f, Attack) + ")";
        case Bright: return (low ? "too dark" : "too bright") + juce::String (" (") + describe (f, Bright) + ")";
        case Low:    return (low ? "thin low end" : "too much low end") + juce::String (" (") + describe (f, Low) + ")";
        case High:   return (low ? "dull top" : "too much top") + juce::String (" (") + describe (f, High) + ")";
        case Dur:    return (low ? "too short" : "very long") + juce::String (" (") + describe (f, Dur) + ")";
        case Pitch:  return f.pitchHz <= 0.0f ? juce::String ("no clear pitch") : "pitched " + juce::String (low ? "low" : "high") + " (" + noteName (f.pitchHz) + ")";
    }
    return {};
}
} // namespace

Fit fitScore (const Features& f, Role role, int genre)
{
    Fit fit;
    if (! f.valid)
        return fit;

    const auto crits = profile (role, genre);
    if (crits.empty())
    {
        fit.score = 70;
        fit.reason = "no role rules for " + juce::String (roleName (role));
        return fit;
    }

    float sumW = 0.0f, sumP = 0.0f, worstP = 0.0f;
    const Crit* worst = nullptr;
    for (const auto& c : crits)
    {
        const float p = penalty (c, value (f, c.feat));
        sumW += c.weight;
        sumP += c.weight * p;
        if (p * c.weight > worstP) { worstP = p * c.weight; worst = &c; }
    }

    fit.score = juce::jlimit (0, 100, juce::roundToInt (100.0f * (1.0f - sumP / sumW)));

    const juce::String target = juce::String (genreName (genre)).upToFirstOccurrenceOf (" (", false, false) + " " + roleName (role);
    if (worst != nullptr && fit.score < 85)
        fit.reason = problem (*worst, f) + " for " + target;
    else
        fit.reason = "good " + target + ": " + describe (f, crits[0].feat) + ", " + describe (f, crits[juce::jmin ((size_t) 1, crits.size() - 1)].feat);

    return fit;
}

juce::String noteName (float hz)
{
    if (hz <= 0.0f) return {};
    const int midi = juce::roundToInt (69.0 + 12.0 * std::log2 ((double) hz / 440.0));
    // FL Studio names middle C (MIDI 60) as C5.
    return juce::MidiMessage::getMidiNoteName (midi, true, true, 5);
}

//==============================================================================
SampleAnalyzer::SampleAnalyzer() : juce::Thread ("Groovebox sample analysis")
{
    fm.registerBasicFormats();
    startThread (juce::Thread::Priority::low);
}

SampleAnalyzer::~SampleAnalyzer()
{
    stopThread (4000);
    saveCache();
}

juce::File SampleAnalyzer::cacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Groovebox").getChildFile ("analysis-cache.tsv");
}

void SampleAnalyzer::enqueue (const juce::Array<juce::File>& files, const juce::File& first)
{
    // The queue is a stack popped from the back: the newest request (the folder the
    // person is digging in right now) is analyzed first, in folder order.
    const juce::ScopedLock sl (cs);
    for (int i = files.size(); --i >= 0;)
        queue.push_back (files.getReference (i));
    if (first.existsAsFile())
        queue.push_back (first);
    notify();
}

bool SampleAnalyzer::get (const juce::File& file, Features& out) const
{
    const juce::ScopedLock sl (cs);
    auto it = results.find (file.getFullPathName().toStdString());
    if (it == results.end()) return false;
    out = it->second.f;
    return out.valid;
}

int SampleAnalyzer::pending() const
{
    const juce::ScopedLock sl (cs);
    return (int) queue.size();
}

void SampleAnalyzer::run()
{
    {
        const juce::ScopedLock sl (cs);
        loadCache();
    }

    while (! threadShouldExit())
    {
        juce::File file;
        {
            const juce::ScopedLock sl (cs);
            if (! queue.empty())
            {
                file = queue.back();
                queue.pop_back();
            }
        }

        if (file == juce::File())
        {
            saveCache(); // builds the text under the lock, writes the file outside it
            wait (400);
            continue;
        }

        const auto key = file.getFullPathName().toStdString();
        const auto size = file.getSize();
        const auto mtime = file.getLastModificationTime().toMilliseconds();
        {
            const juce::ScopedLock sl (cs);
            auto it = results.find (key);
            if (it != results.end() && it->second.size == size && it->second.mtime == mtime)
                continue; // already known
        }

        Entry e;
        e.size = size;
        e.mtime = mtime;

        if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (file) })
        {
            const double sr = r->sampleRate > 0 ? r->sampleRate : 44100.0;
            const int len = (int) juce::jmin (r->lengthInSamples, (juce::int64) (sr * 3.0));
            if (len > 16)
            {
                juce::AudioBuffer<float> buf ((int) juce::jmax (1u, juce::jmin (2u, r->numChannels)), len);
                r->read (&buf, 0, len, 0, true, buf.getNumChannels() > 1);
                if (buf.getNumChannels() > 1)
                {
                    buf.addFrom (0, 0, buf, 1, 0, len);
                    buf.applyGain (0, 0, len, 0.5f);
                }
                e.f = analyzeBuffer (buf.getReadPointer (0), len, sr, (double) r->lengthInSamples / sr);
            }
        }

        {
            const juce::ScopedLock sl (cs);
            results[key] = e;
            dirty = true;
        }
        ++version;
    }
}

void SampleAnalyzer::loadCache()
{
    if (cacheLoaded) return;
    cacheLoaded = true;

    juce::StringArray lines;
    cacheFile().readLines (lines);
    for (const auto& line : lines)
    {
        juce::StringArray t;
        t.addTokens (line, "\t", "");
        if (t.size() < 12) continue;
        Entry e;
        e.size = t[1].getLargeIntValue();
        e.mtime = t[2].getLargeIntValue();
        e.f.valid = t[3].getIntValue() != 0;
        e.f.durSec = t[4].getFloatValue();
        e.f.tailSec = t[5].getFloatValue();
        e.f.attackMs = t[6].getFloatValue();
        e.f.brightHz = t[7].getFloatValue();
        e.f.lowRatio = t[8].getFloatValue();
        e.f.highRatio = t[9].getFloatValue();
        e.f.loudDb = t[10].getFloatValue();
        e.f.pitchHz = t[11].getFloatValue();
        results[t[0].toStdString()] = e;
    }
}

void SampleAnalyzer::saveCache()
{
    juce::String out;
    {
        const juce::ScopedLock sl (cs);
        if (! dirty) return;
        dirty = false;
        out.preallocateBytes ((size_t) results.size() * 160);
        for (const auto& [path, e] : results)
            out << juce::String (path) << '\t' << e.size << '\t' << e.mtime << '\t' << (e.f.valid ? 1 : 0) << '\t'
                << e.f.durSec << '\t' << e.f.tailSec << '\t' << e.f.attackMs << '\t' << e.f.brightHz << '\t'
                << e.f.lowRatio << '\t' << e.f.highRatio << '\t' << e.f.loudDb << '\t' << e.f.pitchHz << '\n';
    }
    auto f = cacheFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (out);
}

} // namespace cg
