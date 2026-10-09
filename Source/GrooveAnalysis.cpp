#include "GrooveAnalysis.h"

namespace cg
{
namespace
{
float sampleAt (const std::vector<float>& v, double pos)
{
    if (pos < 0.0 || pos >= (double) v.size() - 1.0) return 0.0f;
    const int i = (int) pos;
    const float f = (float) (pos - (double) i);
    return v[(size_t) i] + f * (v[(size_t) i + 1] - v[(size_t) i]);
}
} // namespace

Groove analyzeGroove (const float* x, int n, double sr, double forcedBpm, double hintBpm, const juce::String& name)
{
    Groove g;
    g.name = name;
    if (x == nullptr || sr <= 0.0 || n < (int) (sr * 1.5))
        return g;

    // 1. Band envelopes at 400 frames per second.
    const int hop = juce::jmax (1, juce::roundToInt (sr / 400.0));
    const double frameRate = sr / (double) hop;
    const int frames = n / hop;

    juce::IIRFilter lp1, lp2, hpA, hpB, lpA, hp1, hp2;
    lp1.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 150.0));
    lp2.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 150.0));
    hpA.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 250.0));
    hpB.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 250.0)); // steep, so kicks don't leak into the mids
    lpA.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, juce::jmin (3500.0, sr * 0.45)));
    hp1.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, juce::jmin (5000.0, sr * 0.45)));
    hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, juce::jmin (5000.0, sr * 0.45)));

    // Energy per frame, then a trailing RMS window per band that is long enough for its lowest
    // frequencies (otherwise a kick's waveform ripples into fake onsets).
    std::array<std::vector<float>, 3> energy, env;
    for (auto& e : energy) e.assign ((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        double a0 = 0, a1 = 0, a2 = 0;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
        {
            const float s = x[i];
            const float l = lp2.processSingleSampleRaw (lp1.processSingleSampleRaw (s));
            const float m = lpA.processSingleSampleRaw (hpB.processSingleSampleRaw (hpA.processSingleSampleRaw (s)));
            const float h = hp2.processSingleSampleRaw (hp1.processSingleSampleRaw (s));
            a0 += (double) l * l; a1 += (double) m * m; a2 += (double) h * h;
        }
        energy[0][(size_t) f] = (float) (a0 / hop);
        energy[1][(size_t) f] = (float) (a1 / hop);
        energy[2][(size_t) f] = (float) (a2 / hop);
    }
    const int win[3] = { juce::jmax (1, juce::roundToInt (frameRate * 0.020)),
                         juce::jmax (1, juce::roundToInt (frameRate * 0.010)),
                         juce::jmax (1, juce::roundToInt (frameRate * 0.005)) };
    for (int b = 0; b < 3; ++b)
    {
        env[(size_t) b].assign ((size_t) frames, 0.0f);
        double acc = 0.0;
        for (int f = 0; f < frames; ++f)
        {
            acc += energy[(size_t) b][(size_t) f];
            if (f >= win[b]) acc -= energy[(size_t) b][(size_t) (f - win[b])];
            env[(size_t) b][(size_t) f] = (float) std::sqrt (juce::jmax (0.0, acc) / win[b]);
        }
    }

    // 2. Onset strength per band (rise in energy), normalized per band.
    std::array<std::vector<float>, 3> on;
    std::vector<float> combined ((size_t) frames, 0.0f);
    for (int b = 0; b < 3; ++b)
    {
        on[(size_t) b].assign ((size_t) frames, 0.0f);
        float mx = 0.0f;
        for (int f = 1; f < frames; ++f)
        {
            const float d = juce::jmax (0.0f, env[(size_t) b][(size_t) f] - env[(size_t) b][(size_t) f - 1]);
            on[(size_t) b][(size_t) f] = d;
            mx = juce::jmax (mx, d);
        }
        if (mx > 0.0f)
            for (auto& v : on[(size_t) b]) v /= mx;
        for (int f = 0; f < frames; ++f)
            combined[(size_t) f] += on[(size_t) b][(size_t) f];
    }

    // Peak-pick hits per band, then walk back to where each rise starts: that start is the same for a
    // slow kick and a sharp hat, so bands can be compared.
    struct Hit { double pos; float strength; };
    std::array<std::vector<Hit>, 3> hits;
    std::array<std::vector<float>, 3> imp; // hit starts as short bumps, for finding the grid
    const int near = juce::jmax (2, juce::roundToInt (frameRate * 0.025));
    const int gap  = juce::jmax (2, juce::roundToInt (frameRate * 0.040));
    for (int b = 0; b < 3; ++b)
    {
        const auto& o = on[(size_t) b];
        imp[(size_t) b].assign ((size_t) frames, 0.0f);
        int last = -gap;
        for (int f = 1; f + 1 < frames; ++f)
        {
            const float v = o[(size_t) f];
            if (v < 0.12f || f - last < gap) continue;
            bool isMax = true;
            for (int k = juce::jmax (0, f - near); k <= juce::jmin (frames - 1, f + near) && isMax; ++k)
                if (o[(size_t) k] > v || (k < f && o[(size_t) k] >= v)) isMax = false;
            if (! isMax) continue;
            last = f;

            const float thr = v * 0.2f;
            int s0 = f;
            while (s0 > 0 && f - s0 < near * 2 && o[(size_t) s0 - 1] > thr) --s0;
            double pos = (double) s0;
            if (s0 > 0)
            {
                const double lo = o[(size_t) s0 - 1], hi = o[(size_t) s0];
                pos = (double) (s0 - 1) + (hi > lo ? juce::jlimit (0.0, 1.0, ((double) thr - lo) / (hi - lo)) : 1.0);
            }

            float strength = 0.0f;
            for (int k = f; k < juce::jmin (frames, f + win[b] + 8); ++k)
                strength = juce::jmax (strength, env[(size_t) b][(size_t) k]);
            hits[(size_t) b].push_back ({ pos, strength });

            for (int k = -3; k <= 3; ++k)
            {
                const int idx = juce::roundToInt (pos) + k;
                if (idx >= 0 && idx < frames)
                    imp[(size_t) b][(size_t) idx] += v * (1.0f - (float) std::abs (k) / 4.0f);
            }
        }
    }
    std::vector<float> impAll ((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
        impAll[(size_t) f] = imp[0][(size_t) f] + imp[1][(size_t) f] + imp[2][(size_t) f];

    // 3. Tempo.
    double bpm = forcedBpm;
    if (bpm <= 0.0)
    {
        double mean = 0.0;
        for (auto v : combined) mean += v;
        mean /= juce::jmax (1, frames);
        auto ac = [&] (int lag)
        {
            double acc = 0.0;
            for (int f = 0; f + lag < frames; ++f)
                acc += ((double) combined[(size_t) f] - mean) * ((double) combined[(size_t) (f + lag)] - mean);
            return acc / juce::jmax (1, frames - lag);
        };
        const int minLag = (int) (frameRate * 60.0 / 200.0), maxLag = (int) (frameRate * 60.0 / 60.0);
        std::vector<double> score ((size_t) maxLag + 2, 0.0);
        int bestLag = minLag;
        for (int lag = minLag; lag <= maxLag && 2 * lag < frames; ++lag)
        {
            score[(size_t) lag] = ac (lag) + 0.5 * ac (2 * lag);
            if (score[(size_t) lag] > score[(size_t) bestLag]) bestLag = lag;
        }
        double lagRefined = bestLag;
        if (bestLag > minLag && bestLag < maxLag)
        {
            const double a = score[(size_t) bestLag - 1], b = score[(size_t) bestLag], c = score[(size_t) bestLag + 1];
            const double den = a - 2.0 * b + c;
            if (std::abs (den) > 1.0e-12) lagRefined += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den);
        }
        bpm = 60.0 * frameRate / lagRefined;

        // Half/double tempo: if one reading is close to the host tempo, take it (the user is probably
        // working at that tempo); otherwise keep the detected tempo, folded into 70..180.
        double best = bpm;
        while (best > 180.0) best *= 0.5;
        while (best < 70.0) best *= 2.0;
        if (hintBpm > 0.0)
            for (double c : { bpm, bpm * 2.0, bpm * 0.5 })
                if (std::abs (std::log (c / hintBpm)) < 0.08) { best = c; break; }
        bpm = best;
    }
    bpm = juce::jlimit (40.0, 300.0, bpm);
    g.bpm = bpm;

    const double P = 60.0 * frameRate / bpm; // frames per beat
    const double step = P / 4.0;              // frames per 16th

    // 4. Beat phase: where the beats line up with the most onset energy (kicks weigh most).
    double phase = 0.0, bestScore = -1.0;
    for (double ph = 0.0; ph < P; ph += 0.5)
    {
        double sc = 0.0;
        for (double pos = ph; pos < frames; pos += P)
            sc += sampleAt (imp[0], pos) * 1.5 + sampleAt (impAll, pos) * 0.5;
        if (sc > bestScore) { bestScore = sc; phase = ph; }
    }

    // 5. Bar phase: which beat is "1" (kick on 1, snare/clap on 2 and 4).
    int barBeat = 0;
    bestScore = -1.0;
    for (int b = 0; b < 4; ++b)
    {
        double sc = 0.0;
        for (double pos = phase + b * P; pos < frames; pos += 4.0 * P)
            sc += sampleAt (imp[0], pos) + 0.7 * (sampleAt (imp[1], pos + P) + sampleAt (imp[1], pos + 3.0 * P));
        if (sc > bestScore) { bestScore = sc; barBeat = b; }
    }
    double barStart = phase + barBeat * P;
    while (barStart >= 4.0 * P) barStart -= 4.0 * P;

    const double bars = juce::jmax (1.0, ((double) frames - barStart) / (16.0 * step));
    g.bars = (int) std::round (bars);

    // 6. Collect onsets per band onto the 16th grid.
    std::array<std::array<double, 16>, 3> cnt {}, sumOff {}, sumStr {};
    int total = 0;
    for (int b = 0; b < 3; ++b)
        for (const auto& h : hits[(size_t) b])
        {
            const double u = (h.pos - barStart) / step;
            const double slotD = std::round (u);
            const double off = u - slotD;
            if (std::abs (off) > 0.45 || u < -0.45) continue;

            const int s16 = (int) ((((juce::int64) slotD) % 16 + 16) % 16);
            cnt[(size_t) b][(size_t) s16] += 1.0;
            sumOff[(size_t) b][(size_t) s16] += off;
            sumStr[(size_t) b][(size_t) s16] += h.strength;
            ++total;
        }

    // 7. Turn counts into probability, timing and accents.
    for (int b = 0; b < 3; ++b)
    {
        double maxStr = 0.0;
        for (int s = 0; s < 16; ++s)
            if (cnt[(size_t) b][(size_t) s] > 0) maxStr = juce::jmax (maxStr, sumStr[(size_t) b][(size_t) s] / cnt[(size_t) b][(size_t) s]);

        for (int s = 0; s < 16; ++s)
        {
            const double c = cnt[(size_t) b][(size_t) s];
            g.prob[(size_t) b][(size_t) s] = (float) juce::jlimit (0.0, 1.0, c / bars);
            g.has[(size_t) b][(size_t) s] = c >= juce::jmax (1.0, 0.15 * bars);
            g.offset[(size_t) b][(size_t) s] = c > 0 ? (float) (sumOff[(size_t) b][(size_t) s] / c) : 0.0f;
            g.accent[(size_t) b][(size_t) s] = (c > 0 && maxStr > 0.0) ? (float) ((sumStr[(size_t) b][(size_t) s] / c) / maxStr) : 0.0f;
        }
    }

    // 8. Keep only the relative feel: the downbeats (1, 2, 3, 4) define "on time".
    double refSum = 0.0, refW = 0.0;
    for (int b = 0; b < 3; ++b)
        for (int s : { 0, 4, 8, 12 })
            if (g.has[(size_t) b][(size_t) s])
            {
                refSum += g.offset[(size_t) b][(size_t) s] * cnt[(size_t) b][(size_t) s];
                refW += cnt[(size_t) b][(size_t) s];
            }
    const float ref = refW > 0.0 ? (float) (refSum / refW) : 0.0f;
    for (auto& band : g.offset)
        for (auto& o : band)
            o = juce::jlimit (-0.4f, 0.4f, o - ref);

    g.valid = total >= 8;
    return g;
}

Groove analyzeGrooveFile (juce::AudioFormatManager& formats, const juce::File& file, double forcedBpm, double hintBpm)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    if (r == nullptr)
        return {};
    const double sr = r->sampleRate > 0 ? r->sampleRate : 44100.0;
    const int len = (int) juce::jmin (r->lengthInSamples, (juce::int64) (sr * 90.0));
    if (len <= 0)
        return {};

    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, juce::jmin (2u, r->numChannels)), len);
    r->read (&buf, 0, len, 0, true, buf.getNumChannels() > 1);
    if (buf.getNumChannels() > 1)
    {
        buf.addFrom (0, 0, buf, 1, 0, len);
        buf.applyGain (0, 0, len, 0.5f);
    }
    return analyzeGroove (buf.getReadPointer (0), len, sr, forcedBpm, hintBpm, file.getFileNameWithoutExtension());
}

juce::String describeGroove (const Groove& g)
{
    if (! g.valid) return "No groove found";

    auto swingOf = [&g] (int band)
    {
        double odd = 0, even = 0; int no = 0, ne = 0;
        for (int s = 0; s < 16; ++s)
        {
            if (! g.has[(size_t) band][(size_t) s]) continue;
            if (s % 2 == 1) { odd += g.offset[(size_t) band][(size_t) s]; ++no; }
            else            { even += g.offset[(size_t) band][(size_t) s]; ++ne; }
        }
        if (no == 0) return juce::String();
        const int pctLate = juce::roundToInt (100.0 * (odd / no - (ne > 0 ? even / ne : 0.0)));
        return juce::String (pctLate >= 0 ? "+" : "") + juce::String (pctLate) + "%";
    };

    juce::String s;
    s << juce::roundToInt (g.bpm) << " BPM, " << g.bars << " bars";
    const auto hats = swingOf (2), mids = swingOf (1);
    if (hats.isNotEmpty()) s << ", hats offbeat " << hats;
    if (mids.isNotEmpty()) s << ", mids offbeat " << mids;
    if (g.has[1][4] || g.has[1][12])
    {
        const float back = ((g.has[1][4] ? g.offset[1][4] : 0.0f) + (g.has[1][12] ? g.offset[1][12] : 0.0f))
                         / (float) ((g.has[1][4] ? 1 : 0) + (g.has[1][12] ? 1 : 0));
        s << ", backbeat " << (back >= 0 ? "+" : "") << juce::roundToInt (back * 100.0f) << "%";
    }
    return s;
}
} // namespace cg
