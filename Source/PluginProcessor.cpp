#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GrooveAnalysis.h"

using namespace cg;

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout makeParams()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master",
                                                  NormalisableRange<float> (-36.0f, 6.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing amount",
                                                  NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "humanize", 1 }, "Humanize amount",
                                                  NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "seq", 1 }, "Sequencer on", true));

    for (int i = 0; i < kMaxLanes; ++i)
    {
        const String n = "Lane " + String (i + 1) + " ";
        l.add (std::make_unique<AudioParameterFloat> (
            ParameterID { GrooveboxProcessor::paramId (i, "level"), 1 }, n + "Level",
            NormalisableRange<float> (-48.0f, 6.0f, 0.01f), -6.0f,
            AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
        l.add (std::make_unique<AudioParameterFloat> (
            ParameterID { GrooveboxProcessor::paramId (i, "pan"), 1 }, n + "Pan",
            NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const int p = roundToInt (v * 100.0f);
                return p == 0 ? String ("C") : (p < 0 ? "L" + String (-p) : "R" + String (p));
            })));
        l.add (std::make_unique<AudioParameterFloat> (
            ParameterID { GrooveboxProcessor::paramId (i, "pitch"), 1 }, n + "Pitch",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("st")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 2) + " st"; })));
        l.add (std::make_unique<AudioParameterFloat> (
            ParameterID { GrooveboxProcessor::paramId (i, "cutoff"), 1 }, n + "Cutoff",
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const float hz = GrooveboxProcessor::cutoffHz (v);
                return hz >= 1000.0f ? String (hz / 1000.0f, 1) + " kHz" : String (roundToInt (hz)) + " Hz";
            })));
    }
    return l;
}

const char* const kAudioPattern = "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3";

struct NaturalFileOrder
{
    static int compareElements (const juce::File& a, const juce::File& b)
    {
        return a.getFullPathName().compareNatural (b.getFullPathName());
    }
};
} // namespace

juce::AudioProcessor::BusesProperties GrooveboxProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Lane 1 / Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 2; i <= kNumOuts; ++i)
        b = b.withOutput ("Lane " + juce::String (i), juce::AudioChannelSet::stereo(), true); // on by default so FL's Auto map outputs works
    return b;
}

GrooveboxProcessor::GrooveboxProcessor()
    : juce::AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "Params", makeParams())
{
    formats.registerBasicFormats();

    for (int i = 0; i < kMaxLanes; ++i)
    {
        playStep[(size_t) i] = -1;
        auditionFlag[(size_t) i] = false;
        laneMeter[(size_t) i] = 0.0f;
        pLevel[(size_t) i]  = apvts.getRawParameterValue (paramId (i, "level"));
        pPan[(size_t) i]    = apvts.getRawParameterValue (paramId (i, "pan"));
        pPitch[(size_t) i]  = apvts.getRawParameterValue (paramId (i, "pitch"));
        pCutoff[(size_t) i] = apvts.getRawParameterValue (paramId (i, "cutoff"));
    }

    midiOut.ensureSize (8192);
    gen.ref = &groove;
    songMask.fill (0xffffffffu);
    seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    initDefaultLanes();
    generateAll();

    // A fresh Groovebox picks up where you left off: last folders, samples and patterns. Only a new one:
    // when FL opens a project it hands us that project's state, which then wins and the last session is skipped.
    lastSessionPending = lastSessionFile().existsAsFile();

    startTimer (1000);
}

GrooveboxProcessor::~GrooveboxProcessor()
{
    alive->store (false);
    stopTimer();
    io.removeAllJobs (true, 4000);
    if (editorEverOpened)
        autosave();
}

void GrooveboxProcessor::initDefaultLanes()
{
    const Role defaults[] = { Role::Kick, Role::Snare, Role::Clap, Role::ClosedHat, Role::ClosedHat, Role::OpenHat,
                              Role::Ride, Role::Shaker, Role::Perc, Role::Perc, Role::Rim, Role::Tom,
                              Role::Click, Role::Cymbal, Role::Texture, Role::FX };

    const juce::SpinLock::ScopedLockType sl (lock);
    for (int i = 0; i < kMaxLanes; ++i)
        lanes[(size_t) i] = Lane {};

    for (int i = 0; i < 16; ++i)
    {
        lanes[(size_t) i].role = defaults[i];
        lanes[(size_t) i].name = roleName (defaults[i]);
    }
    numLanes = 16;
}

//==============================================================================
void GrooveboxProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& v : voices)
    {
        v.active = false;
        v.smp.reset();
    }
    numPendingOffs = 0;
}

bool GrooveboxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return layouts.inputBuses.isEmpty() || layouts.getMainInputChannelSet().isDisabled();
}

void GrooveboxProcessor::initVoice (Voice& v, int li, float vel, const Step& st, double rate,
                                    float levelDb, float panK, float pitchSt, float cutoffN) const
{
    const auto& lane = lanes[(size_t) li];
    const int len = lane.sample->buffer.getNumSamples();
    const double ratio = std::pow (2.0, (double) (pitchSt + st.pitch) / 12.0) * lane.sample->sampleRate / rate;
    const double startSamples = (double) juce::jlimit (0.0f, 1.0f, st.start) * 0.5 * (double) len;

    v.active = true;
    v.smp = lane.sample;
    if (lane.reverse) { v.pos = (double) (len - 2) - startSamples; v.inc = -ratio; }
    else              { v.pos = startSamples;                      v.inc = ratio; }

    const float velGain = juce::Decibels::decibelsToGain (-30.0f * (1.0f - juce::jlimit (0.0f, 1.0f, vel)));
    const float g = juce::Decibels::decibelsToGain (levelDb, -60.0f) * velGain;
    const float pan = juce::jlimit (-1.0f, 1.0f, panK + st.pan);
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    v.gainL = g * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    v.gainR = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;

    // Filter: the lane filter, or an automatic low-pass when only per-step cutoff is used.
    v.ftype = lane.filter;
    float norm = cutoffN;
    if (v.ftype == FilterType::Off && st.cutoff < 0.999f) { v.ftype = FilterType::LowPass; norm = 1.0f; }
    if (v.ftype != FilterType::Off)
    {
        const float fc = juce::jlimit (20.0f, (float) (rate * 0.45),
                                       cutoffHz (norm) * std::pow (2.0f, -(1.0f - juce::jlimit (0.0f, 1.0f, st.cutoff)) * 6.0f));
        const float gg = std::tan (juce::MathConstants<float>::pi * fc / (float) rate);
        v.k  = 2.0f - 1.9f * juce::jlimit (0.0f, 1.0f, lane.reso);
        v.a1 = 1.0f / (1.0f + gg * (gg + v.k));
        v.a2 = gg * v.a1;
        v.a3 = gg * v.a2;
        v.ic1[0] = v.ic1[1] = v.ic2[0] = v.ic2[1] = 0.0f;
    }

    v.lane = li;
    v.chokeGroup = lane.choke;
    const int outIdx = lane.out < 0 ? li : lane.out;
    v.out = juce::jlimit (0, kNumOuts - 1, outIdx);
    v.startOffset = 0;
    v.chokeAt = -1;
    v.age = 0;
    v.attackSamples = (int) (rate * 0.2 * (double) (lane.attack * lane.attack));
    v.decaySamples = lane.decay >= 0.999f ? std::numeric_limits<int>::max()
                                          : (int) (rate * 0.005 * std::pow (400.0, (double) lane.decay));
    v.fading = false;
    v.fadeLen = juce::jmax (1, (int) (rate * 0.012));
    v.fadeLeft = v.fadeLen;

}

void GrooveboxProcessor::renderVoice (Voice& v, float* oL, float* oR, int from, int to, double rate)
{
    const auto& b = v.smp->buffer;
    const int len = b.getNumSamples();
    const float* sL = b.getReadPointer (0);
    const float* sR = b.getReadPointer (1);

    for (int i = from; i < to; ++i)
    {
        if (v.chokeAt >= 0 && i >= v.chokeAt && ! v.fading)
        {
            v.fading = true;
            v.fadeLen = juce::jmax (1, (int) (rate * 0.004));
            v.fadeLeft = v.fadeLen;
        }
        if (! v.fading && v.age >= v.decaySamples)
        {
            v.fading = true;
            v.fadeLeft = v.fadeLen;
        }

        const int ip = (int) std::floor (v.pos);
        if (ip < 0 || ip + 1 >= len) { v.active = false; break; }

        const float fr = (float) (v.pos - (double) ip);
        float l = sL[ip] + fr * (sL[ip + 1] - sL[ip]);
        float r = sR[ip] + fr * (sR[ip + 1] - sR[ip]);

        if (v.ftype != FilterType::Off)
        {
            float* io[2] = { &l, &r };
            for (int c = 0; c < 2; ++c)
            {
                const float x = *io[c];
                const float v3 = x - v.ic2[c];
                const float v1 = v.a1 * v.ic1[c] + v.a2 * v3;
                const float v2 = v.ic2[c] + v.a2 * v.ic1[c] + v.a3 * v3;
                v.ic1[c] = 2.0f * v1 - v.ic1[c];
                v.ic2[c] = 2.0f * v2 - v.ic2[c];
                *io[c] = v.ftype == FilterType::LowPass ? v2
                       : v.ftype == FilterType::BandPass ? v1
                                                         : x - v.k * v1 - v2;
            }
        }

        float e = 1.0f;
        if (v.attackSamples > 0 && v.age < v.attackSamples)
            e = (float) v.age / (float) v.attackSamples;
        if (v.fading)
        {
            e *= (float) v.fadeLeft / (float) v.fadeLen;
            if (--v.fadeLeft <= 0) { v.active = false; break; }
        }

        oL[i] += l * v.gainL * e;
        oR[i] += r * v.gainR * e;
        v.pos += v.inc;
        ++v.age;
    }

}

// Beds: textures that run underneath everything (crackle, rain, room tone), looped with a crossfade,
// fading in and out with the transport and with song sections.
void GrooveboxProcessor::renderBeds (std::array<float*, cg::kNumOuts>& outL, std::array<float*, cg::kNumOuts>& outR,
                                     int n, bool running, bool song)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    const int nl = numLanes.load();
    bool anySolo = false;
    for (int li = 0; li < nl; ++li) anySolo = anySolo || lanes[(size_t) li].solo;
    const int bar = currentBar.load();

    for (int li = 0; li < kMaxLanes; ++li)
    {
        auto& b = beds[(size_t) li];
        if (li >= nl) { b.gain = 0.0f; continue; }
        const auto& lane = lanes[(size_t) li];
        if (! lane.isBed() || lane.sample == nullptr || lane.sample->buffer.getNumSamples() < 256)
        {
            b.gain = 0.0f;
            continue;
        }
        const bool audible = running && ! lane.mute && ! (anySolo && ! lane.solo) && (! song || laneActiveAtBar (li, bar));
        const float target = audible ? 1.0f : 0.0f;
        if (target == 0.0f && b.gain < 1.0e-5f) { b.gain = 0.0f; continue; }

        const auto& smp = *lane.sample;
        const int len = smp.buffer.getNumSamples();
        const float* sL = smp.buffer.getReadPointer (0);
        const float* sR = smp.buffer.getReadPointer (1);
        const int xf = juce::jmax (16, juce::jmin (len / 4, (int) (smp.sampleRate * 0.3)));
        const double ratio = std::pow (2.0, (double) pPitch[(size_t) li]->load() / 12.0) * smp.sampleRate / sr;
        const float g = juce::Decibels::decibelsToGain (pLevel[(size_t) li]->load(), -60.0f);
        const float angle = (juce::jlimit (-1.0f, 1.0f, pPan[(size_t) li]->load()) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gl = g * std::cos (angle) * juce::MathConstants<float>::sqrt2;
        const float gr = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;

        float a1 = 0, a2 = 0, a3 = 0, kk = 1;
        const bool filt = lane.filter != FilterType::Off;
        if (filt)
        {
            const float fc = juce::jlimit (20.0f, (float) (sr * 0.45), cutoffHz (pCutoff[(size_t) li]->load()));
            const float gg = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
            kk = 2.0f - 1.9f * juce::jlimit (0.0f, 1.0f, lane.reso);
            a1 = 1.0f / (1.0f + gg * (gg + kk));
            a2 = gg * a1;
            a3 = gg * a2;
        }

        const int outIdx = juce::jlimit (0, kNumOuts - 1, lane.out < 0 ? li : lane.out);
        float* oL = outL[(size_t) outIdx];
        float* oR = outR[(size_t) outIdx];
        const float k = 1.0f / (float) (sr * 0.4);
        auto read = [len] (const float* d, double p)
        {
            const int ip = juce::jlimit (0, len - 2, (int) p);
            const float fr = (float) (p - (double) ip);
            return d[ip] + fr * (d[ip + 1] - d[ip]);
        };

        for (int i = 0; i < n; ++i)
        {
            b.gain += (target - b.gain) * k;
            if (b.pos >= (double) len) b.pos -= (double) (len - xf);
            float l = read (sL, b.pos), r = read (sR, b.pos);
            if (b.pos >= (double) (len - xf))
            {
                const float a = (float) ((b.pos - (double) (len - xf)) / (double) xf);
                const double q = b.pos - (double) (len - xf);
                l = l * (1.0f - a) + read (sL, q) * a;
                r = r * (1.0f - a) + read (sR, q) * a;
            }
            if (filt)
            {
                float* io[2] = { &l, &r };
                for (int c = 0; c < 2; ++c)
                {
                    const float x = *io[c];
                    const float v3 = x - b.ic2[c];
                    const float v1 = a1 * b.ic1[c] + a2 * v3;
                    const float v2 = b.ic2[c] + a2 * b.ic1[c] + a3 * v3;
                    b.ic1[c] = 2.0f * v1 - b.ic1[c];
                    b.ic2[c] = 2.0f * v2 - b.ic2[c];
                    *io[c] = lane.filter == FilterType::LowPass ? v2 : lane.filter == FilterType::BandPass ? v1 : x - kk * v1 - v2;
                }
            }
            oL[i] += l * gl * b.gain;
            oR[i] += r * gr * b.gain;
            b.pos += ratio;
        }
        laneMeter[(size_t) li] = juce::jmax (laneMeter[(size_t) li].load(), b.gain * 0.4f);
    }
}

void GrooveboxProcessor::triggerVoice (int li, float vel, int offset, const Step& st)
{
    const auto& lane = lanes[(size_t) li];
    if (lane.sample == nullptr || lane.sample->buffer.getNumSamples() < 4)
        return;

    if (lane.choke > 0)
        for (auto& v : voices)
            if (v.active && v.chokeGroup == lane.choke && ! v.fading)
                v.chokeAt = juce::jmax (offset, v.startOffset);

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }

    if (target == nullptr) // steal the oldest
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.age > target->age) target = &v;
    }

    initVoice (*target, li, vel, st, sr, pLevel[(size_t) li]->load(), pPan[(size_t) li]->load(),
               pPitch[(size_t) li]->load(), pCutoff[(size_t) li]->load());
    target->startOffset = offset;

    laneMeter[(size_t) li] = juce::jmax (laneMeter[(size_t) li].load(), vel);
}

void GrooveboxProcessor::sendLaneMidi (int li, float vel, int offset, const Step& st, int lengthSamples)
{
    const auto& lane = lanes[(size_t) li];
    const int ch = juce::jlimit (1, 16, lane.midiChannel);
    const int note = juce::jlimit (0, 127, lane.midiNote + juce::roundToInt (pPitch[(size_t) li]->load() + st.pitch));

    // If this note is still held, release it first so it can retrigger.
    for (int i = 0; i < numPendingOffs; ++i)
        if (pendingOffs[(size_t) i].channel == ch && pendingOffs[(size_t) i].note == note)
        {
            midiOut.addEvent (juce::MidiMessage::noteOff (ch, note), offset);
            pendingOffs[(size_t) i] = pendingOffs[(size_t) --numPendingOffs];
            break;
        }

    midiOut.addEvent (juce::MidiMessage::noteOn (ch, note, juce::jlimit (0.01f, 1.0f, vel)), offset);

    if (numPendingOffs < (int) pendingOffs.size())
        pendingOffs[(size_t) numPendingOffs++] = { offset + juce::jmax (1, lengthSamples), ch, note };

    laneMeter[(size_t) li] = juce::jmax (laneMeter[(size_t) li].load(), vel);
}

void GrooveboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
    midiOut.clear();

    // Output pointers per stereo pair (pairs the host hasn't enabled fall back to Main).
    std::array<float*, kNumOuts> outL {}, outR {};
    const int mainIdx = getChannelIndexInProcessBlockBuffer (false, 0, 0);
    for (int i = 0; i < kNumOuts; ++i)
    {
        auto* bus = getBus (false, i);
        const bool on = (i == 0 || separateOuts.load()) && bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2;
        const int idx = on ? getChannelIndexInProcessBlockBuffer (false, i, 0) : mainIdx;
        outL[(size_t) i] = buffer.getWritePointer (idx);
        outR[(size_t) i] = buffer.getWritePointer (idx + 1);
    }

    double bpm = 124.0, ppq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
        }
    bpm = juce::jlimit (20.0, 999.0, bpm);

    const bool preview = previewPlaying.load();
    const bool seqOn = apvts.getRawParameterValue ("seq")->load() > 0.5f;
    const bool pianoRoll = pianoRollMode.load();
    const bool running = seqOn && ! pianoRoll && (hostPlaying || preview);

    hostIsPlaying = hostPlaying;
    if (! hostPlaying)
    {
        if (seekPending.exchange (false) || ! preview)
            internalPpq = songMode.load() ? songCursor.load() : 0.0;
        if (preview) { ppq = internalPpq; bpm = juce::jlimit (40.0, 300.0, previewBpm.load()); }
    }

    lastBpm = bpm;
    const double beatsPerSample = bpm / 60.0 / sr;
    const double ppqEnd = ppq + (double) n * beatsPerSample;
    const bool song = songMode.load();
    currentBar = running ? (int) std::floor (ppq / 4.0) : -1;

    // A jump backwards means the host looped: change the random salt so probabilities re-roll each pass.
    if (ppq < lastPpqEnd - 1.0e-6)
        ++passCounter;
    lastPpqEnd = ppqEnd;
    const juce::uint32 passSeed = seed ^ ((juce::uint32) passCounter * 2654435761u);

    const float swingScale = apvts.getRawParameterValue ("swing")->load();
    const float humScale   = apvts.getRawParameterValue ("humanize")->load();
    const Step defaultStep {};

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const int nl = numLanes.load();

        auto fire = [&] (int li, float vel, int off, const Step& st, double stepBeats)
        {
            if (lanes[(size_t) li].mode == LaneMode::FLChannel)
            {
                const int lenSamples = (int) juce::jmin (sr * 0.25, stepBeats / beatsPerSample * 0.9);
                sendLaneMidi (li, vel, off, st, lenSamples);
            }
            else
            {
                triggerVoice (li, vel, off, st);
            }
        };

        // Notes from FL (piano roll mode): use the step settings of the step each note sits on.
        auto stepAtTime = [&] (int li, int offset) -> Step
        {
            const auto& lane = lanes[(size_t) li];
            if (! hostPlaying) return defaultStep;
            const double t = ppq + (double) offset * beatsPerSample;
            const double sb = lane.stepBeats();
            const auto k = (juce::int64) std::floor (t / sb + 0.5);
            if (k < 0) return defaultStep;
            Step st = lane.steps[(size_t) (k % juce::jlimit (1, kMaxSteps, lane.length))];
            if (! st.on) return defaultStep;
            st.ratchet = 1; // ratchets arrive as their own notes
            return st;
        };

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                const int li = m.getNoteNumber() - kBaseNote;
                if (li >= 0 && li < nl && ! lanes[(size_t) li].mute)
                {
                    const int off = juce::jlimit (0, n - 1, meta.samplePosition);
                    fire (li, m.getFloatVelocity(), off, stepAtTime (li, off), lanes[(size_t) li].stepBeats());
                }
            }
        }

        for (int li = 0; li < nl; ++li)
            if (auditionFlag[(size_t) li].exchange (false))
                fire (li, 0.9f, 0, defaultStep, 0.5);

        bool anySolo = false;
        for (int li = 0; li < nl; ++li)
            anySolo = anySolo || lanes[(size_t) li].solo;

        for (int li = 0; li < nl; ++li)
        {
            const auto& lane = lanes[(size_t) li];
            if (! running)
            {
                playStep[(size_t) li] = -1;
                continue;
            }

            const double sb = lane.stepBeats();
            const int len = juce::jlimit (1, kMaxSteps, lane.length);
            playStep[(size_t) li] = (int) (((juce::int64) std::floor (juce::jmax (0.0, ppq) / sb)) % len);

            if (lane.mute || (anySolo && ! lane.solo))
                continue;
            if (lane.mode == LaneMode::Sample && lane.sample == nullptr)
                continue;

            forEachHit (lanes.data(), nl, li, passSeed, ppq, ppqEnd, bpm, swingScale, humScale,
                           [&] (double t, float v, const Step& st, double grid)
                           {
                               if (song && ! laneActiveAtBar (li, (int) std::floor (grid / 4.0 + 1.0e-9)))
                                   return; // this lane rests in this part of the song
                               const int off = juce::jlimit (0, n - 1, (int) ((t - ppq) / beatsPerSample));
                               fire (li, v, off, st, sb);
                           },
                           0.0, &groove, grooveAmount);
        }
    }

    // Release MIDI notes whose time has come (all of them when the sequencer stops).
    for (int i = 0; i < numPendingOffs;)
    {
        auto& p = pendingOffs[(size_t) i];
        if (! running || p.samplesLeft < n)
        {
            midiOut.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), running ? juce::jlimit (0, n - 1, p.samplesLeft) : 0);
            p = pendingOffs[(size_t) --numPendingOffs];
        }
        else
        {
            p.samplesLeft -= n;
            ++i;
        }
    }

    // Render voices.
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        renderVoice (v, outL[(size_t) v.out], outR[(size_t) v.out], v.startOffset, n, sr);

        v.startOffset = 0;
        v.chokeAt = -1;
        if (! v.active)
            v.smp.reset(); // never the last reference: lanes or `retired` still hold it
    }

    renderBeds (outL, outR, n, running, song);

    if (! hostPlaying && preview)
        internalPpq = ppqEnd;

    const float master = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("master")->load(), -60.0f);
    buffer.applyGain (master);

    midi.swapWith (midiOut);
}

//==============================================================================
cg::SamplePtr GrooveboxProcessor::loadSample (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 1)
        return {};

    const auto maxLen = (juce::int64) (reader->sampleRate * 30.0);
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);

    auto sd = std::make_shared<SampleData>();
    sd->buffer.setSize (2, len);
    sd->buffer.clear();
    reader->read (&sd->buffer, 0, len, 0, true, true);
    if (reader->numChannels == 1)
        sd->buffer.copyFrom (1, 0, sd->buffer, 0, 0, len);
    sd->sampleRate = reader->sampleRate;
    sd->file = f;
    return sd;
}

void GrooveboxProcessor::swapSample (int li, cg::SamplePtr sp)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (lanes[(size_t) li].sample, sp);
    }
    if (sp != nullptr)
        retired.push_back (std::move (sp));
}

void GrooveboxProcessor::timerCallback()
{
    // No project state arrived in the first moments: this is a new Groovebox, so bring back the last session.
    if (lastSessionPending.load() && ++timerTicks >= 2)
        loadLastSessionIfNew();

    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [] (const cg::SamplePtr& p) { return p.use_count() <= 1; }),
                   retired.end());
}

//==============================================================================
cg::Fit GrooveboxProcessor::fileFit (const juce::File& f, Role role) const
{
    Features feat;
    if (! analyzer.get (f, feat))
        return {};
    return fitScore (feat, role, gen.genre);
}

cg::Fit GrooveboxProcessor::laneFit (int li) const
{
    if (li < 0 || li >= numLanes) return {};
    const auto& lane = lanes[(size_t) li];
    if (lane.sample == nullptr) return {};
    return fileFit (lane.sample->file, lane.role);
}

int GrooveboxProcessor::pickGoodSample (int li)
{
    const auto& lane = lanes[(size_t) li];
    const int count = lane.files.size();
    if (count == 0) return -1;

    std::vector<std::pair<int, int>> scored; // (score, index)
    for (int i = 0; i < count; ++i)
    {
        const auto fit = fileFit (lane.files.getReference (i), lane.role);
        if (fit.score >= 0) scored.push_back ({ fit.score, i });
    }

    if (scored.size() < 3)
        return rng.nextInt (count); // not analyzed yet: plain random

    std::sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first > b.first; });

    // Good fits (70+), or at least the top quarter of what's been analyzed.
    size_t pool = 0;
    while (pool < scored.size() && scored[pool].first >= 70) ++pool;
    pool = juce::jmax (pool, juce::jmax ((size_t) 1, scored.size() / 4));

    // Avoid handing back the current sample when there's a choice.
    for (int tries = 0; tries < 8; ++tries)
    {
        const int idx = scored[(size_t) rng.nextInt ((int) pool)].second;
        if (idx != lane.fileIndex || pool == 1) return idx;
    }
    return scored[0].second;
}

juce::Array<juce::File> GrooveboxProcessor::listAudioFiles (const juce::File& folder, bool recursive)
{
    juce::Array<juce::File> files;
    if (! folder.isDirectory())
        return files;

    // Capped walk: a huge folder can never stall anything.
    for (const auto& entry : juce::RangedDirectoryIterator (folder, recursive, kAudioPattern, juce::File::findFiles))
    {
        if (entry.isHidden()) continue;
        files.add (entry.getFile());
        if (files.size() >= 5000) break;
    }
    NaturalFileOrder order;
    files.sort (order);
    return files;
}

//==============================================================================
juce::String GrooveboxProcessor::grooveToString (const cg::Groove& g)
{
    juce::String s;
    for (int b = 0; b < 3; ++b)
        for (int i = 0; i < 16; ++i)
            s << juce::String (g.prob[(size_t) b][(size_t) i], 4) << ':' << juce::String (g.offset[(size_t) b][(size_t) i], 4) << ':'
              << juce::String (g.accent[(size_t) b][(size_t) i], 4) << ':' << (g.has[(size_t) b][(size_t) i] ? 1 : 0) << ',';
    return s;
}

void GrooveboxProcessor::grooveFromString (cg::Groove& g, const juce::String& str)
{
    juce::StringArray items;
    items.addTokens (str, ",", "");
    items.removeEmptyStrings();
    if (items.size() < 48) { g.valid = false; return; }
    for (int k = 0; k < 48; ++k)
    {
        juce::StringArray f;
        f.addTokens (items[k], ":", "");
        const auto b = (size_t) (k / 16), i = (size_t) (k % 16);
        g.prob[b][i]   = juce::jlimit (0.0f, 1.0f, f[0].getFloatValue());
        g.offset[b][i] = juce::jlimit (-0.4f, 0.4f, f[1].getFloatValue());
        g.accent[b][i] = juce::jlimit (0.0f, 1.0f, f[2].getFloatValue());
        g.has[b][i]    = f[3].getIntValue() != 0;
    }
    g.valid = true;
}

void GrooveboxProcessor::loadGroove (const juce::File& file, double forcedBpm)
{
    if (! file.existsAsFile())
        return;

    const int token = ++grooveToken;
    const double hint = lastBpm.load();
    auto apply = [this, file, forcedBpm, token] (const cg::Groove& g)
    {
        grooveBusy = false;
        if (token != grooveToken) return; // superseded
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            grooveFile = file;
            grooveForcedBpm = forcedBpm;
            if (g.valid) groove = g;
        }
        if (g.valid)
        {
            // The reference brings its own timing, so the global swing steps aside.
            if (auto* p = apvts.getParameter ("swing")) p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
            previewBpm = juce::jlimit (40.0, 300.0, g.bpm);
        }
        grooveMessage = g.valid ? "Groove from " + g.name + ": " + cg::describeGroove (g)
                                : "Couldn't find a groove in " + file.getFileName() + " (try a drum loop, or set its BPM)";
        ++modelVersion;
    };

    grooveBusy = true;
    if (runSynchronously)
    {
        apply (cg::analyzeGrooveFile (formats, file, forcedBpm, hint));
        return;
    }

    ++ioBusy;
    auto aliveFlag = alive;
    io.addJob ([this, aliveFlag, file, forcedBpm, hint, apply]
    {
        juce::AudioFormatManager fm; // own manager: formats is used on the message thread
        fm.registerBasicFormats();
        const auto g = cg::analyzeGrooveFile (fm, file, forcedBpm, hint);
        juce::MessageManager::callAsync ([aliveFlag, this, g, apply]
        {
            if (! aliveFlag->load()) return;
            --ioBusy;
            apply (g);
        });
    });
}

void GrooveboxProcessor::clearGroove()
{
    ++grooveToken;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        groove = cg::Groove {};
        grooveFile = juce::File();
        grooveForcedBpm = 0.0;
    }
    ++modelVersion;
}

void GrooveboxProcessor::setLaneFolder (int li, const juce::File& folder, bool guessRoleFromName, const juce::File& preferredFile)
{
    if (li < 0 || li >= numLanes || ! folder.isDirectory())
        return;

    {
        // Remember the folder right away so autosave never loses it while the scan runs.
        const juce::SpinLock::ScopedLockType sl (lock);
        lanes[(size_t) li].folder = folder;
        lanes[(size_t) li].name = folder.getFileName();
        if (preferredFile != juce::File()) lanes[(size_t) li].pendingFile = preferredFile;
    }

    const int token = ++laneToken[(size_t) li];

    if (runSynchronously)
    {
        applyLaneFolder (li, folder, listAudioFiles (folder), guessRoleFromName, preferredFile);
        return;
    }

    ++ioBusy;
    const int gen = stateGeneration;
    auto aliveFlag = alive;
    io.addJob ([this, aliveFlag, li, folder, guessRoleFromName, preferredFile, token, gen]
    {
        auto files = listAudioFiles (folder);
        juce::MessageManager::callAsync ([this, aliveFlag, li, folder, guessRoleFromName, preferredFile, token, gen, files]
        {
            if (! aliveFlag->load()) return;
            --ioBusy;
            if (token != laneToken[(size_t) li] || gen != stateGeneration) return; // superseded
            applyLaneFolder (li, folder, files, guessRoleFromName, preferredFile);
        });
    });
}

void GrooveboxProcessor::applyLaneFolder (int li, const juce::File& folder, const juce::Array<juce::File>& files,
                                          bool guessRoleFromName, const juce::File& preferredFile)
{
    if (li < 0 || li >= numLanes)
        return;

    juce::Array<juce::File> incoming (files); // copied outside the lock, swapped in below
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& lane = lanes[(size_t) li];
        lane.folder = folder;
        lane.files.swapWith (incoming);
        lane.fileIndex = -1;
        lane.pendingFile = juce::File();
        lane.name = folder.getFileName();
        if (guessRoleFromName)
        {
            auto r = guessRole (folder.getFileName());
            if (r == Role::Other) r = guessRole (folder.getParentDirectory().getFileName());
            if (r != Role::Other) lane.role = r;
        }
    }

    if (files.isEmpty())
    {
        swapSample (li, nullptr);
    }
    else
    {
        const int preferred = preferredFile != juce::File() ? files.indexOf (preferredFile) : -1;
        loadSampleIndex (li, preferred >= 0 ? preferred : 0);
    }

    analyzer.enqueue (files, files.isEmpty() ? juce::File() : files[juce::jmax (0, lanes[(size_t) li].fileIndex)]);
    ++modelVersion;
}

void GrooveboxProcessor::loadSampleIndex (int li, int index)
{
    if (li < 0 || li >= numLanes)
        return;

    auto& lane = lanes[(size_t) li];
    const int count = lane.files.size();
    if (count == 0)
        return;

    index = ((index % count) + count) % count;

    // Skip unreadable files in the scroll direction.
    for (int tries = 0; tries < juce::jmin (count, 20); ++tries)
    {
        const int idx = (index + tries) % count;
        if (auto sp = loadSample (lane.files[idx]))
        {
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                lane.fileIndex = idx;
            }
            swapSample (li, std::move (sp));
            return;
        }
    }
}

void GrooveboxProcessor::stepSample (int li, int delta)
{
    if (li < 0 || li >= numLanes || lanes[(size_t) li].files.isEmpty())
        return;

    const auto& lane = lanes[(size_t) li];
    const int count = lane.files.size();
    int idx = lane.fileIndex + delta;

    // With the fit filter on, skip samples known to fit poorly (unanalyzed ones still show).
    if (fitFilter)
        for (int tries = 0; tries < count; ++tries)
        {
            const int wrapped = ((idx % count) + count) % count;
            const auto fit = fileFit (lane.files.getReference (wrapped), lane.role);
            if (fit.score < 0 || fit.score >= 50) break;
            idx += delta;
        }

    loadSampleIndex (li, idx);
    audition (li);
}

void GrooveboxProcessor::randomSample (int li)
{
    if (li < 0 || li >= numLanes || lanes[(size_t) li].files.isEmpty() || lanes[(size_t) li].lockSample)
        return;
    loadSampleIndex (li, pickGoodSample (li));
    audition (li);
}

void GrooveboxProcessor::shuffleAllSamples()
{
    for (int li = 0; li < numLanes; ++li)
        if (! lanes[(size_t) li].files.isEmpty() && ! lanes[(size_t) li].lockSample)
            loadSampleIndex (li, pickGoodSample (li));
    ++modelVersion;
}

std::vector<std::pair<juce::File, juce::Array<juce::File>>> GrooveboxProcessor::findLibraryFolders (const juce::File& root)
{
    // Breadth-first, max 3 levels deep, capped: picking a whole drive can't freeze FL.
    std::vector<std::pair<juce::File, juce::Array<juce::File>>> found;
    std::vector<std::pair<juce::File, int>> todo { { root, 0 } };
    int visited = 0;

    for (size_t q = 0; q < todo.size() && visited < 3000 && found.size() < 300; ++q)
    {
        const auto [dir, depth] = todo[q];
        ++visited;

        auto files = listAudioFiles (dir, false);
        if (! files.isEmpty())
            found.push_back ({ dir, files });

        if (depth < 3)
            for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findDirectories))
                if (! entry.isHidden())
                    todo.push_back ({ entry.getFile(), depth + 1 });
    }

    NaturalFileOrder order;
    std::sort (found.begin(), found.end(), [&order] (const auto& a, const auto& b)
               { return order.compareElements (a.first, b.first) < 0; });
    return found;
}

void GrooveboxProcessor::scanLibrary (const juce::File& root)
{
    if (! root.isDirectory())
        return;
    libraryRoot = root;

    if (runSynchronously)
    {
        applyLibrary (root, findLibraryFolders (root));
        return;
    }

    ++ioBusy;
    const int token = ++libraryToken;
    const int gen = stateGeneration;
    auto aliveFlag = alive;
    io.addJob ([this, aliveFlag, root, token, gen]
    {
        auto dirs = findLibraryFolders (root);
        juce::MessageManager::callAsync ([this, aliveFlag, root, token, gen, dirs]
        {
            if (! aliveFlag->load()) return;
            --ioBusy;
            if (token != libraryToken || gen != stateGeneration) return;
            applyLibrary (root, dirs);
        });
    });
}

//==============================================================================
namespace
{
struct KitLane
{
    cg::Role role;
    const char* name;
    int follow = -1;          // index in the kit
    float chance = 0.35f, delayMs = 40.0f, drift = 0.35f, vel = 0.5f;
    float levelDb = -6.0f;
};

std::vector<KitLane> starterKit (int genre)
{
    using R = cg::Role;
    switch (genre)
    {
        case cg::Microhouse:
            return { { R::Kick, "Kick" }, { R::Clap, "Clap", -1, 0, 0, 0, 0, -8 }, { R::ClosedHat, "Hat", -1, 0, 0, 0, 0, -9 },
                     { R::OpenHat, "Open hat", -1, 0, 0, 0, 0, -12 }, { R::Shaker, "Shaker", -1, 0, 0, 0, 0, -14 },
                     { R::Rim, "Rim", -1, 0, 0, 0, 0, -10 }, { R::Click, "Clicks", -1, 0, 0, 0, 0, -14 }, { R::Perc, "Perc", -1, 0, 0, 0, 0, -11 },
                     { R::Foley, "Foley after rim", 5, 0.35f, 25.0f, 0.4f, 0.55f, -12 },
                     { R::Foley, "Foley after kick", 0, 0.25f, 70.0f, 0.5f, 0.35f, -14 },
                     { R::Foley, "Foley loose", -1, 0, 0, 0, 0, -16 },
                     { R::Bed, "Room / vinyl bed", -1, 0, 0, 0, 0, -22 } };
        case cg::DeepDusty:
            return { { R::Kick, "Kick" }, { R::Clap, "Clap", -1, 0, 0, 0, 0, -8 }, { R::ClosedHat, "Hat", -1, 0, 0, 0, 0, -10 },
                     { R::OpenHat, "Open hat", -1, 0, 0, 0, 0, -13 }, { R::Ride, "Ride", -1, 0, 0, 0, 0, -15 },
                     { R::Shaker, "Shaker", -1, 0, 0, 0, 0, -15 }, { R::Perc, "Perc", -1, 0, 0, 0, 0, -12 }, { R::Rim, "Rim", -1, 0, 0, 0, 0, -12 },
                     { R::Foley, "Foley after clap", 1, 0.4f, 35.0f, 0.4f, 0.5f, -12 },
                     { R::Foley, "Foley after kick", 0, 0.3f, 80.0f, 0.5f, 0.35f, -14 },
                     { R::Foley, "Foley loose", -1, 0, 0, 0, 0, -16 },
                     { R::Bed, "Crackle bed", -1, 0, 0, 0, 0, -20 }, { R::Bed, "Room tone bed", -1, 0, 0, 0, 0, -24 } };
        case cg::FutureGarage:
            return { { R::Kick, "Kick" }, { R::Snare, "Snare", -1, 0, 0, 0, 0, -7 }, { R::Clap, "Clap", -1, 0, 0, 0, 0, -10 },
                     { R::ClosedHat, "Hat", -1, 0, 0, 0, 0, -10 }, { R::Shaker, "Shaker", -1, 0, 0, 0, 0, -14 },
                     { R::Rim, "Rim", -1, 0, 0, 0, 0, -12 }, { R::Perc, "Perc", -1, 0, 0, 0, 0, -13 },
                     { R::Foley, "Foley after snare", 1, 0.5f, 30.0f, 0.45f, 0.55f, -11 },
                     { R::Foley, "Foley after kick", 0, 0.3f, 70.0f, 0.5f, 0.35f, -14 },
                     { R::Foley, "Foley after hat", 3, 0.2f, 15.0f, 0.3f, 0.4f, -16 },
                     { R::Foley, "Foley loose", -1, 0, 0, 0, 0, -16 },
                     { R::Bed, "Rain bed", -1, 0, 0, 0, 0, -20 }, { R::Bed, "Crackle bed", -1, 0, 0, 0, 0, -22 },
                     { R::Vox, "Vocal chops", -1, 0, 0, 0, 0, -10 } };
        default:
            return { { R::Kick, "Kick" }, { R::Snare, "Snare", -1, 0, 0, 0, 0, -8 }, { R::Clap, "Clap", -1, 0, 0, 0, 0, -8 },
                     { R::ClosedHat, "Hat", -1, 0, 0, 0, 0, -9 }, { R::OpenHat, "Open hat", -1, 0, 0, 0, 0, -12 },
                     { R::Ride, "Ride", -1, 0, 0, 0, 0, -14 }, { R::Shaker, "Shaker", -1, 0, 0, 0, 0, -14 },
                     { R::Perc, "Perc", -1, 0, 0, 0, 0, -11 }, { R::Rim, "Rim", -1, 0, 0, 0, 0, -12 },
                     { R::Foley, "Foley after clap", 2, 0.3f, 35.0f, 0.4f, 0.45f, -13 },
                     { R::Foley, "Foley loose", -1, 0, 0, 0, 0, -16 },
                     { R::Bed, "Texture bed", -1, 0, 0, 0, 0, -22 } };
    }
}
} // namespace

void GrooveboxProcessor::withLibrary (std::function<void()> then)
{
    if (! libraryDirs.empty() || ! libraryRoot.isDirectory())
    {
        then();
        return;
    }
    if (runSynchronously)
    {
        libraryDirs = findLibraryFolders (libraryRoot);
        then();
        return;
    }
    ++ioBusy;
    const auto root = libraryRoot;
    const int gen = stateGeneration;
    auto aliveFlag = alive;
    io.addJob ([this, aliveFlag, root, gen, then]
    {
        auto dirs = findLibraryFolders (root);
        juce::MessageManager::callAsync ([this, aliveFlag, gen, dirs, then]
        {
            if (! aliveFlag->load()) return;
            --ioBusy;
            if (gen != stateGeneration) return;
            libraryDirs = dirs;
            then();
        });
    });
}

static cg::Role dirRoleOf (const juce::File& d)
{
    auto r = cg::guessRole (d.getFileName());
    return r != cg::Role::Other ? r : cg::guessRole (d.getParentDirectory().getFileName());
}

void GrooveboxProcessor::fillEmptyLanesFromLibrary()
{
    std::vector<int> useCount (libraryDirs.size(), 0);
    for (int li = 0; li < numLanes; ++li)
        for (size_t d = 0; d < libraryDirs.size(); ++d)
            if (libraryDirs[d].first == lanes[(size_t) li].folder) ++useCount[d];

    for (int li = 0; li < numLanes; ++li)
    {
        if (! lanes[(size_t) li].files.isEmpty() || lanes[(size_t) li].lockSample) continue;
        // Foliage roles fall back to related folders when the library has none of their own.
        const auto role = lanes[(size_t) li].role;
        std::vector<Role> tryRoles { role };
        if (role == Role::Bed)   tryRoles.push_back (Role::Texture);
        if (role == Role::Foley) { tryRoles.push_back (Role::Click); tryRoles.push_back (Role::Perc); tryRoles.push_back (Role::Rim); }
        int best = -1;
        for (auto want : tryRoles)
        {
            for (size_t d = 0; d < libraryDirs.size(); ++d)
                if (dirRoleOf (libraryDirs[d].first) == want && (best < 0 || useCount[d] < useCount[(size_t) best]))
                    best = (int) d;
            if (best >= 0) break;
        }
        if (best < 0) continue;
        ++useCount[(size_t) best];
        ++laneToken[(size_t) li];
        const auto name = lanes[(size_t) li].name;
        applyLaneFolder (li, libraryDirs[(size_t) best].first, libraryDirs[(size_t) best].second, false, {});
        lanes[(size_t) li].name = name; // keep the kit's lane name ("Foley after snare")
        loadSampleIndex (li, pickGoodSample (li));
    }
}

void GrooveboxProcessor::applyStarterKit (int genre)
{
    const auto kit = starterKit (genre);
    const int nl = numLanes.load();
    const int k = juce::jmin ((int) kit.size(), kMaxLanes);

    // Reuse samples you already picked for the same role.
    std::vector<bool> taken ((size_t) nl, false);
    std::vector<Lane> fresh ((size_t) k);
    for (int i = 0; i < k; ++i)
    {
        const auto& kl = kit[(size_t) i];
        auto& l = fresh[(size_t) i];
        l.role = kl.role;
        l.name = kl.name;
        if (kl.follow >= 0)
        {
            l.follow = kl.follow;
            l.followChance = kl.chance;
            l.followDelay = kl.delayMs;
            l.followDrift = kl.drift;
            l.followVel = kl.vel;
        }
        if (kl.role == Role::ClosedHat || kl.role == Role::OpenHat) l.choke = 1;
        for (int j = 0; j < nl; ++j)
            if (! taken[(size_t) j] && lanes[(size_t) j].role == kl.role && lanes[(size_t) j].sample != nullptr)
            {
                taken[(size_t) j] = true;
                const auto& o = lanes[(size_t) j];
                l.folder = o.folder; l.files = o.files; l.fileIndex = o.fileIndex; l.sample = o.sample;
                l.lockSample = o.lockSample;
                break;
            }
    }

    std::vector<SamplePtr> dropped;
    std::vector<juce::Array<juce::File>> oldFiles;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int j = 0; j < kMaxLanes; ++j)
        {
            if (lanes[(size_t) j].sample != nullptr) dropped.push_back (lanes[(size_t) j].sample);
            oldFiles.emplace_back();
            lanes[(size_t) j].files.swapWith (oldFiles.back());
            lanes[(size_t) j].sample = nullptr;
        }
        for (int i = 0; i < k; ++i)
            std::swap (lanes[(size_t) i], fresh[(size_t) i]);
        for (int j = k; j < kMaxLanes; ++j)
            lanes[(size_t) j] = Lane {};
        numLanes = k;
        songMask.fill (0xffffffffu);
    }
    for (auto& d : dropped) retired.push_back (std::move (d));
    for (auto& t : laneToken) ++t;

    for (int i = 0; i < kMaxLanes; ++i)
    {
        resetLaneParams (i);
        if (i < k)
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (paramId (i, "level"))))
                p->setValueNotifyingHost (p->convertTo0to1 (kit[(size_t) i].levelDb));
    }

    gen.genre = genre;
    previewBpm = genreBpm (genre);
    generateAll();
    if (songMode.load()) makeSong (songBars);

    withLibrary ([this] { fillEmptyLanesFromLibrary(); ++modelVersion; });
    ++modelVersion;
}

void GrooveboxProcessor::randomizeAll()
{
    withLibrary ([this]
    {
        auto isDrum = [] (Role r) { return r != Role::Foley && r != Role::Bed && r != Role::Vox && r != Role::Texture
                                        && r != Role::FX && r != Role::Other; };
        for (int li = 0; li < numLanes; ++li)
        {
            auto& l = lanes[(size_t) li];
            if (l.lockSample) continue;

            // Mostly a different folder for the same instrument; sometimes a different instrument altogether.
            std::vector<int> same, other;
            for (size_t d = 0; d < libraryDirs.size(); ++d)
            {
                const auto r = dirRoleOf (libraryDirs[d].first);
                if (r == l.role) same.push_back ((int) d);
                else if (isDrum (r) && isDrum (l.role) && r != Role::Kick && l.role != Role::Kick) other.push_back ((int) d);
            }
            int pick = -1;
            if (! other.empty() && rng.nextFloat() < 0.3f) pick = other[(size_t) rng.nextInt ((int) other.size())];
            else if (! same.empty()) pick = same[(size_t) rng.nextInt ((int) same.size())];

            if (pick >= 0 && libraryDirs[(size_t) pick].first != l.folder)
            {
                const auto r = dirRoleOf (libraryDirs[(size_t) pick].first);
                const bool keepName = l.follow >= 0;
                const auto name = l.name;
                {
                    const juce::SpinLock::ScopedLockType sl (lock);
                    l.role = r;
                }
                ++laneToken[(size_t) li];
                applyLaneFolder (li, libraryDirs[(size_t) pick].first, libraryDirs[(size_t) pick].second, false, {});
                if (keepName) l.name = name;
            }
            if (! l.files.isEmpty())
                loadSampleIndex (li, pickGoodSample (li));
        }
        generateAll();
        ++modelVersion;
    });
}

void GrooveboxProcessor::applyLibrary (const juce::File& root, const std::vector<std::pair<juce::File, juce::Array<juce::File>>>& dirs)
{
    juce::ignoreUnused (root);
    libraryDirs = dirs;

    auto dirRole = [] (const juce::File& d)
    {
        auto r = guessRole (d.getFileName());
        return r != Role::Other ? r : guessRole (d.getParentDirectory().getFileName());
    };

    std::vector<bool> used (dirs.size(), false);

    // Folders already used by sample-locked lanes stay with them.
    for (int li = 0; li < numLanes; ++li)
        if (lanes[(size_t) li].lockSample)
            for (size_t d = 0; d < dirs.size(); ++d)
                if (dirs[d].first == lanes[(size_t) li].folder) used[d] = true;

    auto assign = [&] (int li, size_t d, bool guess)
    {
        ++laneToken[(size_t) li]; // cancels any scan still running for this lane
        applyLaneFolder (li, dirs[d].first, dirs[d].second, guess, {});
        used[d] = true;
    };

    // 1) match lanes to folders by role
    for (int li = 0; li < numLanes; ++li)
    {
        if (lanes[(size_t) li].lockSample) continue;
        for (size_t d = 0; d < dirs.size(); ++d)
            if (! used[d] && dirRole (dirs[d].first) == lanes[(size_t) li].role)
            {
                assign (li, d, false);
                break;
            }
    }

    // 2) empty lanes take the leftovers, 3) then add lanes for anything still unused
    size_t next = 0;
    auto nextUnused = [&]() -> int
    {
        while (next < dirs.size() && used[next]) ++next;
        return next < dirs.size() ? (int) next : -1;
    };

    for (int li = 0; li < numLanes; ++li)
        if (lanes[(size_t) li].files.isEmpty() && ! lanes[(size_t) li].lockSample
            && (lanes[(size_t) li].folder == juce::File() || ! lanes[(size_t) li].folder.isDirectory()))
            if (int d = nextUnused(); d >= 0)
                assign (li, (size_t) d, true);

    while (numLanes < kMaxLanes)
    {
        const int d = nextUnused();
        if (d < 0) break;
        addLane();
        assign (numLanes - 1, (size_t) d, true);
    }

    ++modelVersion;
}

//==============================================================================
void GrooveboxProcessor::newSeed()
{
    seed = (juce::uint32) rng.nextInt();
}

void GrooveboxProcessor::generateAll()
{
    newSeed();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int li = 0; li < numLanes; ++li)
            if (! lanes[(size_t) li].lockPattern)
                cg::generateLane (lanes[(size_t) li], gen, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::generateLane (int li)
{
    if (li < 0 || li >= numLanes || lanes[(size_t) li].lockPattern) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        cg::generateLane (lanes[(size_t) li], gen, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::mutateAll()
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int li = 0; li < numLanes; ++li)
            if (! lanes[(size_t) li].lockPattern)
                cg::mutateLane (lanes[(size_t) li], mutateAmount, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::mutateOneLane (int li)
{
    if (li < 0 || li >= numLanes || lanes[(size_t) li].lockPattern) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        cg::mutateLane (lanes[(size_t) li], mutateAmount, rng);
    }
    ++modelVersion;
}

void GrooveboxProcessor::makeSong (int bars)
{
    enum Type { Intro, Build, Main, Break, Outro };
    static const char* names[] = { "Intro", "Build", "Main", "Break", "Outro" };

    bars = juce::jlimit (16, 256, bars);
    std::vector<std::pair<int, int>> plan; // (type, bars)
    if (bars <= 32)       plan = { { Intro, 4 }, { Build, 4 }, { Main, 8 }, { Break, 4 }, { Main, 8 }, { Outro, 4 } };
    else if (bars <= 64)  plan = { { Intro, 8 }, { Build, 8 }, { Main, 16 }, { Break, 8 }, { Main, 16 }, { Outro, 8 } };
    else if (bars <= 96)  plan = { { Intro, 16 }, { Build, 8 }, { Main, 24 }, { Break, 16 }, { Build, 8 }, { Main, 16 }, { Outro, 8 } };
    else                  plan = { { Intro, 16 }, { Build, 16 }, { Main, 32 }, { Break, 16 }, { Build, 8 }, { Main, 24 }, { Outro, 16 } };

    int total = 0;
    for (auto& p : plan) total += p.second;
    if (bars != total) // stretch the main sections to fit other lengths
        for (auto& p : plan)
            if (p.first == Main) { p.second = juce::jmax (4, p.second + (bars - total) / 2); }
    total = 0;
    for (auto& p : plan) total += p.second;

    // Which roles play in which kind of section (chance per lane, decided once per section).
    auto chance = [] (Role r, int type) -> float
    {
        switch (type)
        {
            case Intro:
                switch (r) { case Role::Kick: case Role::ClosedHat: case Role::Click: case Role::Texture: return 1.0f;
                             case Role::Perc: case Role::Rim: case Role::Shaker: return 0.5f; default: return 0.0f; }
            case Build:
                switch (r) { case Role::Texture: case Role::OpenHat: case Role::Ride: case Role::Bass: return 0.5f;
                             case Role::Snare: case Role::Clap: return 0.6f; case Role::Tom: return 0.3f; case Role::Cymbal: return 0.0f;
                             default: return 1.0f; }
            case Main:
                return r == Role::Texture ? 0.5f : 1.0f;
            case Break:
                switch (r) { case Role::Kick: case Role::Bass: case Role::Snare: return 0.0f;
                             case Role::Texture: case Role::FX: case Role::Ride: case Role::Cymbal: return 1.0f;
                             case Role::ClosedHat: case Role::OpenHat: return 0.4f; case Role::Clap: return 0.3f; default: return 0.5f; }
            case Outro:
                switch (r) { case Role::Kick: case Role::ClosedHat: case Role::Texture: return 1.0f;
                             case Role::Perc: case Role::Rim: return 0.5f; default: return 0.0f; }
            default: return 1.0f;
        }
    };

    std::array<juce::uint32, 512> mask {};
    std::vector<Section> sections;
    const int nl = numLanes;
    int bar = 0;
    for (auto [type, len] : plan)
    {
        juce::uint32 m = 0;
        for (int li = 0; li < nl; ++li)
            if (rng.nextFloat() < chance (lanes[(size_t) li].role, type)) m |= (1u << li);
        if (m == 0 && nl > 0) m = 1u; // never a fully silent section

        for (int b = bar; b < bar + len && b < (int) mask.size(); ++b)
            mask[(size_t) b] = m;

        // Tension: kick and bass drop out for the last bar of a build.
        if (type == Build && len >= 4)
            for (int li = 0; li < nl; ++li)
                if (lanes[(size_t) li].role == Role::Kick || lanes[(size_t) li].role == Role::Bass)
                    mask[(size_t) (bar + len - 1)] &= ~(1u << li);

        sections.push_back ({ names[type], bar, len, type });
        bar += len;
    }

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        songMask = mask;
        songBars = total;
    }
    songSections = sections;
    ++modelVersion;
}

void GrooveboxProcessor::copyLaneParams (int from, int to)
{
    for (auto* what : { "level", "pan", "pitch", "cutoff" })
        if (auto* src = apvts.getParameter (paramId (from, what)))
            if (auto* dst = apvts.getParameter (paramId (to, what)))
                dst->setValueNotifyingHost (src->getValue());
}

void GrooveboxProcessor::resetLaneParams (int li)
{
    for (auto* what : { "level", "pan", "pitch", "cutoff" })
        if (auto* p = apvts.getParameter (paramId (li, what)))
            p->setValueNotifyingHost (p->getDefaultValue());
}

void GrooveboxProcessor::addLane()
{
    const int nl = numLanes;
    if (nl >= kMaxLanes) return;
    resetLaneParams (nl);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        lanes[(size_t) nl] = Lane {};
        lanes[(size_t) nl].role = Role::Perc;
        lanes[(size_t) nl].name = "Lane " + juce::String (nl + 1);
        // Own output, unless a lane already uses that one: then the first free output.
        std::array<bool, kNumOuts> used {};
        for (int j = 0; j < nl; ++j)
            used[(size_t) juce::jlimit (0, kNumOuts - 1, lanes[(size_t) j].out < 0 ? j : lanes[(size_t) j].out)] = true;
        if (nl < kNumOuts && used[(size_t) nl])
            for (int o = 1; o < kNumOuts; ++o)
                if (! used[(size_t) o]) { lanes[(size_t) nl].out = o; break; }
        for (auto& m : songMask) m |= (1u << nl); // a new lane plays throughout the song
        numLanes = nl + 1;
    }
    ++modelVersion;
}

int GrooveboxProcessor::duplicateLane (int li)
{
    const int nl = numLanes;
    if (li < 0 || li >= nl || nl >= kMaxLanes) return -1;

    Lane dup = lanes[(size_t) li]; // the message thread is the only writer, so reading without the lock is safe
    dup.solo = false;
    if (dup.out < 0) dup.out = li; // the copy shares the original's mixer track
    if (! dup.name.endsWith (" copy")) dup.name << " copy";
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int j = li + 1; j < nl; ++j) // lanes below keep their mixer track
            if (lanes[(size_t) j].out < 0) lanes[(size_t) j].out = j;
        for (int j = nl; j > li + 1; --j)
            lanes[(size_t) j] = std::move (lanes[(size_t) j - 1]);
        lanes[(size_t) li + 1] = std::move (dup);
        numLanes = nl + 1;
        for (int j = 0; j <= nl; ++j) // followers keep following the same lane
            if (j != li + 1 && lanes[(size_t) j].follow > li) ++lanes[(size_t) j].follow;
        if (lanes[(size_t) li + 1].follow > li) ++lanes[(size_t) li + 1].follow;
        // The copy plays in the same song sections as the original.
        const juce::uint32 below = (1u << (li + 1)) - 1u;
        for (auto& m : songMask)
        {
            const juce::uint32 bit = (m >> li) & 1u;
            m = (m & below) | ((m & ~below) << 1) | (bit << (li + 1));
        }
    }
    for (auto& t : laneToken) ++t; // pending folder scans refer to old lane positions

    for (int j = nl; j > li + 1; --j)
        copyLaneParams (j - 1, j);
    copyLaneParams (li, li + 1);
    ++modelVersion;
    return li + 1;
}

void GrooveboxProcessor::removeLane (int li)
{
    const int nl = numLanes;
    if (li < 0 || li >= nl || nl <= 1) return;

    cg::SamplePtr old;
    Lane removed;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = lanes[(size_t) li].sample;
        // Lanes below keep the output (mixer track) they had, instead of sliding up to the next one.
        for (int j = li + 1; j < nl; ++j)
            if (lanes[(size_t) j].out < 0) lanes[(size_t) j].out = j;
        removed = std::move (lanes[(size_t) li]);
        for (int j = li; j < nl - 1; ++j)
            lanes[(size_t) j] = std::move (lanes[(size_t) j + 1]);
        lanes[(size_t) nl - 1] = Lane {};
        numLanes = nl - 1;
        for (int j = 0; j < nl - 1; ++j) // followers keep following the same lane
        {
            auto& fo = lanes[(size_t) j].follow;
            if (fo == li) fo = -1;
            else if (fo > li) --fo;
        }
        const juce::uint32 below = (1u << li) - 1u;
        for (auto& m : songMask) m = (m & below) | ((m >> 1) & ~below);
    }

    for (auto& t : laneToken) ++t; // pending folder scans refer to old lane positions

    // Level, pan, pitch and cutoff move up with their lanes.
    for (int j = li; j < nl - 1; ++j)
        copyLaneParams (j + 1, j);
    resetLaneParams (nl - 1);

    if (old != nullptr)
        retired.push_back (std::move (old));
    removed.sample.reset();
    ++modelVersion;
}

//==============================================================================
juce::File GrooveboxProcessor::exportFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Groovebox").getChildFile ("MIDI");
    dir.createDirectory();
    return dir;
}

juce::File GrooveboxProcessor::exportMidi()
{
    struct Ev { double t; float v; int note; double len = 0.0; };
    std::vector<Ev> events;
    events.reserve (4096);

    const bool song = songMode.load();
    const double total = (song ? songBars : juce::jlimit (1, 64, exportBars)) * 4.0;
    const float swingScale = apvts.getRawParameterValue ("swing")->load();
    const float humScale   = apvts.getRawParameterValue ("humanize")->load();

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const int nl = numLanes;
        bool anySolo = false;
        for (int li = 0; li < nl; ++li) anySolo = anySolo || lanes[(size_t) li].solo;

        for (int li = 0; li < nl; ++li)
        {
            const auto& lane = lanes[(size_t) li];
            if (lane.mute || (anySolo && ! lane.solo)) continue;
            if (lane.isBed())
            {
                if (lane.sample != nullptr) events.push_back ({ 0.0, 0.8f, kBaseNote + li, total - 1.0 / 960.0 });
                continue;
            }
            // Every step exactly once, wrapped into the clip, so the MIDI loops seamlessly
            // (a step nudged early at the clip start lands at the clip end).
            if (song)
            {
                // A song is not a loop: export it as it plays.
                forEachHit (lanes.data(), nl, li, seed, -1.0, total + 1.0, lastBpm.load(), swingScale, humScale,
                               [&] (double t, float v, const Step&, double grid)
                               {
                                   if (laneActiveAtBar (li, (int) std::floor (grid / 4.0 + 1.0e-9)))
                                       events.push_back ({ juce::jlimit (0.0, total - 1.0 / 960.0, t), v, kBaseNote + li });
                               },
                               0.0, &groove, grooveAmount);
                continue;
            }
            forEachHit (lanes.data(), nl, li, seed, 0.0, total, lastBpm.load(), swingScale, humScale,
                           [&] (double t, float v, const Step&, double)
                           {
                               t = std::fmod (t, total);
                               if (t < 0.0) t += total;
                               events.push_back ({ juce::jmin (t, total - 1.0 / 960.0), v, kBaseNote + li });
                           },
                           total, &groove, grooveAmount);
        }
    }

    juce::MidiMessageSequence seq;
    constexpr double tpq = 960.0;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / lastBpm.load())), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (const auto& e : events)
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, e.note, juce::jlimit (0.01f, 1.0f, e.v)), e.t * tpq);
        seq.addEvent (juce::MidiMessage::noteOff (1, e.note), (e.t + (e.len > 0.0 ? e.len : juce::jlimit (0.01, 0.125, total - e.t))) * tpq);
    }
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote ((int) tpq);
    mf.addTrack (seq);

    auto name = juce::String ("Groovebox ") + juce::String (genreName (gen.genre)).upToFirstOccurrenceOf (" (", false, false)
              + (song ? " song " + juce::String (songBars) : " " + juce::String (exportBars)) + " bars " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".mid";
    auto file = exportFolder().getChildFile (juce::File::createLegalFileName (name).replaceCharacter ('/', '-'));

    file.deleteFile();
    juce::FileOutputStream out (file);
    if (out.openedOk())
    {
        mf.writeTo (out);
        out.flush();
    }
    return file;
}

//==============================================================================
// Sending lanes to FL as normal channels: the sample (with the lane's sound baked in) goes to the
// Channel Rack, the notes go to that channel's piano roll.
juce::File GrooveboxProcessor::flExportFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Groovebox").getChildFile ("To FL");
    dir.createDirectory();
    return dir;
}

int GrooveboxProcessor::exportLengthBars() const
{
    if (songMode.load()) return songBars;
    double beats = 4.0 * gen.bars;
    for (int li = 0; li < numLanes; ++li)
        beats = juce::jmax (beats, lanes[(size_t) li].length * lanes[(size_t) li].stepBeats());
    return juce::jlimit (1, 64, (int) std::ceil (beats / 4.0 - 1.0e-9));
}

juce::String GrooveboxProcessor::laneExportName (int li) const
{
    const auto& l = lanes[(size_t) li];
    // What FL shows as the channel name: the instrument, then the sample ("Snare - snare_04").
    juce::String n = roleName (l.role);
    if (l.follow >= 0 || (l.name.isNotEmpty() && ! l.name.containsIgnoreCase (n) && l.name.startsWith ("Foley")))
        n = l.name; // "Foley after snare"
    if (l.sample != nullptr)
        n << " - " << l.sample->file.getFileNameWithoutExtension();
    return juce::File::createLegalFileName (n).replaceCharacter ('/', '-').substring (0, 60).trim();
}

juce::File GrooveboxProcessor::bakeLaneSample (int li)
{
    if (li < 0 || li >= numLanes) return {};

    SamplePtr smp;
    Voice v;
    juce::String name, key;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const auto& l = lanes[(size_t) li];
        if (l.sample == nullptr || l.sample->buffer.getNumSamples() < 4) return {};
        smp = l.sample;
        const float lvl = pLevel[(size_t) li]->load(), pan = pPan[(size_t) li]->load();
        const float pit = pPitch[(size_t) li]->load(), cut = pCutoff[(size_t) li]->load();
        initVoice (v, li, 1.0f, Step {}, smp->sampleRate, lvl, pan, pit, cut);
        name = laneExportName (li);
        key << smp->file.getFullPathName() << lvl << pan << pit << cut << (int) l.filter << l.reso << l.attack << l.decay << (int) l.reverse;
    }

    // Long enough for the whole sample at its pitch, plus the fade.
    const int maxLen = (int) juce::jmin ((double) smp->sampleRate * 20.0,
                                         std::abs ((double) smp->buffer.getNumSamples() / v.inc) + smp->sampleRate * 0.05);
    juce::AudioBuffer<float> out (2, juce::jmax (16, maxLen));
    out.clear();
    renderVoice (v, out.getWritePointer (0), out.getWritePointer (1), 0, out.getNumSamples(), smp->sampleRate);
    v.smp.reset();

    // Trim the silent tail.
    int end = out.getNumSamples();
    while (end > 16 && std::abs (out.getSample (0, end - 1)) < 3.0e-5f && std::abs (out.getSample (1, end - 1)) < 3.0e-5f)
        --end;

    // One folder per baked sound, so the file (and FL's channel) carries just the readable name.
    const auto hash = juce::String::toHexString (key.hashCode64()).getLastCharacters (8);
    auto dir = flExportFolder().getChildFile ("Sounds").getChildFile (hash);
    dir.createDirectory();
    auto file = dir.getChildFile (name + ".wav");
    if (file.existsAsFile()) return file; // same sound already baked

    juce::WavAudioFormat wav;
    auto tmp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".tmp");
    {
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (tmp);
        if (! static_cast<juce::FileOutputStream*> (os.get())->openedOk()) return {};
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), smp->sampleRate, 2, 24, {}, 0));
        if (w == nullptr) return {};
        os.release();
        w->writeFromAudioSampleBuffer (out, 0, end);
    }
    tmp.moveFileTo (file);
    return file;
}

juce::Array<juce::File> GrooveboxProcessor::bakeAllLaneSamples()
{
    juce::Array<juce::File> files;
    bool anySolo = false;
    for (int li = 0; li < numLanes; ++li) anySolo = anySolo || lanes[(size_t) li].solo;
    for (int li = 0; li < numLanes; ++li)
    {
        const auto& l = lanes[(size_t) li];
        if (l.mute || (anySolo && ! l.solo) || l.sample == nullptr) continue;
        const auto f = bakeLaneSample (li);
        if (f.existsAsFile()) files.add (f);
    }
    return files;
}

juce::File GrooveboxProcessor::exportLaneMidi (int li)
{
    if (li < 0 || li >= numLanes) return {};

    struct Ev { double t; float v; int note; double len = 0.0; };
    std::vector<Ev> events;
    const bool song = songMode.load();
    const int bars = exportLengthBars();
    const double total = bars * 4.0;
    const float swingScale = apvts.getRawParameterValue ("swing")->load();
    const float humScale   = apvts.getRawParameterValue ("humanize")->load();
    juce::String name;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const auto& lane = lanes[(size_t) li];
        name = laneExportName (li);
        // FL's sampler plays a sample at its own pitch on C5 (MIDI 60); step pitch moves the note.
        auto noteFor = [] (const Step& st) { return juce::jlimit (0, 127, 60 + juce::roundToInt (st.pitch)); };
        const int nl = numLanes.load();
        if (lane.isBed())
            events.push_back ({ 0.0, 0.8f, 60, total - 1.0 / 960.0 }); // one long note: the bed under everything
        else if (song)
            forEachHit (lanes.data(), nl, li, seed, -1.0, total + 1.0, lastBpm.load(), swingScale, humScale,
                           [&] (double t, float v, const Step& st, double grid)
                           {
                               if (laneActiveAtBar (li, (int) std::floor (grid / 4.0 + 1.0e-9)))
                                   events.push_back ({ juce::jlimit (0.0, total - 1.0 / 960.0, t), v, noteFor (st) });
                           },
                           0.0, &groove, grooveAmount);
        else
            forEachHit (lanes.data(), nl, li, seed, 0.0, total, lastBpm.load(), swingScale, humScale,
                           [&] (double t, float v, const Step& st, double)
                           {
                               t = std::fmod (t, total);
                               if (t < 0.0) t += total;
                               events.push_back ({ juce::jmin (t, total - 1.0 / 960.0), v, noteFor (st) });
                           },
                           total, &groove, grooveAmount);
    }

    juce::MidiMessageSequence seq;
    constexpr double tpq = 960.0;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / lastBpm.load())), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (const auto& e : events)
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, e.note, juce::jlimit (0.01f, 1.0f, e.v)), e.t * tpq);
        seq.addEvent (juce::MidiMessage::noteOff (1, e.note), (e.t + (e.len > 0.0 ? e.len : juce::jlimit (0.01, 0.125, total - e.t))) * tpq);
    }
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote ((int) tpq);
    mf.addTrack (seq);
    auto file = flExportFolder().getChildFile (name + " notes " + juce::String (bars) + (bars == 1 ? " bar" : " bars") + ".mid");
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (! os.openedOk()) return {};
    mf.writeTo (os);
    os.flush();
    return file;
}

//==============================================================================
void GrooveboxProcessor::applyGrooveToLanes (bool copyPattern)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (! groove.valid) return;
        for (int li = 0; li < numLanes; ++li)
        {
            auto& l = lanes[(size_t) li];
            if (l.lockPattern) continue;
            const int band = bandFor (l.role);

            if (copyPattern)
            {
                if (l.role == Role::Texture || l.role == Role::FX || l.role == Role::Other || l.role == Role::Cymbal)
                    continue; // long sounds: keep what they have
                const auto tpl = referenceTemplate (groove, l.role);
                l.length = 16;
                l.rate = kRate16;
                for (auto& st : l.steps) st = Step {};
                for (int i = 0; i < 16; ++i)
                {
                    const float p = tpl[(size_t) i];
                    if (p < 0.2f) continue;
                    auto& st = l.steps[(size_t) i];
                    st.on = true;
                    st.prob = p >= 0.85f ? 1.0f : p;
                    st.vel = groove.has[(size_t) band][(size_t) i] ? 0.35f + 0.65f * groove.accent[(size_t) band][(size_t) i] : 0.6f;
                    st.nudge = juce::jlimit (-0.5f, 0.5f, grooveOffset (groove, band, i));
                }
            }
            else
            {
                // Timing and accents only: every hit that sits on a 16th takes the reference's push or drag.
                const double sb = l.stepBeats();
                for (int k = 0; k < l.length; ++k)
                {
                    auto& st = l.steps[(size_t) k];
                    if (! st.on) continue;
                    const double g16 = (double) k * sb / 0.25;
                    const double r16 = std::round (g16);
                    if (std::abs (g16 - r16) > 0.26) continue;
                    const int slot = (int) (((juce::int64) r16) % 16);
                    st.nudge = juce::jlimit (-0.5f, 0.5f, (float) (grooveOffset (groove, band, slot) * 0.25 / sb));
                    if (groove.has[(size_t) band][(size_t) slot])
                        st.vel = juce::jlimit (0.05f, 1.0f, st.vel * (0.55f + 0.9f * groove.accent[(size_t) band][(size_t) slot]));
                }
            }
            l.swing = 0.0f;
        }
        grooveAmount = 0.0f; // the feel now lives in the steps; don't apply it twice
    }
    ++modelVersion;
}

//==============================================================================
juce::String GrooveboxProcessor::stepsToString (const Lane& lane)
{
    juce::String s;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto& st = lane.steps[(size_t) i];
        if (! st.on) continue;
        s << i << ':' << juce::String (st.vel, 6) << ':' << juce::String (st.prob, 6) << ':'
          << juce::String (st.nudge, 6) << ':' << st.ratchet << ':' << juce::String (st.pitch, 4) << ':'
          << juce::String (st.pan, 6) << ':' << juce::String (st.cutoff, 6) << ':' << juce::String (st.start, 6) << ';';
    }
    return s;
}

void GrooveboxProcessor::stepsFromString (Lane& lane, const juce::String& s)
{
    for (auto& st : lane.steps) st = Step {};

    juce::StringArray items;
    items.addTokens (s, ";", "");
    for (const auto& item : items)
    {
        juce::StringArray f;
        f.addTokens (item, ":", "");
        if (f.size() < 5) continue;
        const int i = f[0].getIntValue();
        if (i < 0 || i >= kMaxSteps) continue;
        auto& st = lane.steps[(size_t) i];
        st.on = true;
        st.vel = juce::jlimit (0.0f, 1.0f, f[1].getFloatValue());
        st.prob = juce::jlimit (0.0f, 1.0f, f[2].getFloatValue());
        st.nudge = juce::jlimit (-0.5f, 0.5f, f[3].getFloatValue());
        st.ratchet = juce::jlimit (1, 4, f[4].getIntValue());
        if (f.size() >= 9)
        {
            st.pitch = juce::jlimit (-12.0f, 12.0f, f[5].getFloatValue());
            st.pan = juce::jlimit (-1.0f, 1.0f, f[6].getFloatValue());
            st.cutoff = juce::jlimit (0.0f, 1.0f, f[7].getFloatValue());
            st.start = juce::jlimit (0.0f, 1.0f, f[8].getFloatValue());
        }
    }
}

std::unique_ptr<juce::XmlElement> GrooveboxProcessor::createStateXml()
{
    auto xmlPtr = std::make_unique<juce::XmlElement> ("Groovebox");
    auto& xml = *xmlPtr;
    xml.setAttribute ("version", 4);
    if (auto params = apvts.copyState().createXml())
        xml.addChildElement (params.release());

    xml.setAttribute ("genre", gen.genre);
    xml.setAttribute ("density", gen.density);
    xml.setAttribute ("variation", gen.variation);
    xml.setAttribute ("irregular", gen.irregular);
    xml.setAttribute ("mutate", mutateAmount);
    xml.setAttribute ("seed", juce::String ((juce::int64) seed));
    xml.setAttribute ("library", libraryRoot.getFullPathName());
    xml.setAttribute ("exportBars", exportBars);
    xml.setAttribute ("fitFilter", fitFilter);
    xml.setAttribute ("bars", gen.bars);
    xml.setAttribute ("previewBpm", previewBpm.load());
    xml.setAttribute ("songMode", songMode.load());
    xml.setAttribute ("pianoRollMode", pianoRollMode.load());
    xml.setAttribute ("songBars", songBars);
    {
        juce::String sec, masks;
        for (const auto& sc : songSections)
            sec << sc.name << ':' << sc.startBar << ':' << sc.bars << ':' << sc.type << ';';
        for (int b = 0; b < songBars && b < (int) songMask.size(); ++b)
            masks << juce::String::toHexString ((juce::int64) songMask[(size_t) b]) << ',';
        xml.setAttribute ("songSections", sec);
        xml.setAttribute ("songMask", masks);
    }

    const juce::SpinLock::ScopedLockType sl (lock);
    xml.setAttribute ("separateOuts", separateOuts.load());
    xml.setAttribute ("grooveAmount", grooveAmount);
    xml.setAttribute ("grooveFile", grooveFile.getFullPathName());
    xml.setAttribute ("grooveForcedBpm", grooveForcedBpm);
    if (groove.valid)
    {
        xml.setAttribute ("grooveName", groove.name);
        xml.setAttribute ("grooveBpm", groove.bpm);
        xml.setAttribute ("grooveBars", groove.bars);
        xml.setAttribute ("grooveData", grooveToString (groove));
    }
    xml.setAttribute ("numLanes", numLanes.load());
    for (int li = 0; li < numLanes; ++li)
    {
        const auto& l = lanes[(size_t) li];
        auto* e = xml.createNewChildElement ("Lane");
        e->setAttribute ("role", (int) l.role);
        e->setAttribute ("name", l.name);
        e->setAttribute ("length", l.length);
        e->setAttribute ("rate", l.rate);
        e->setAttribute ("decay", l.decay);
        e->setAttribute ("attack", l.attack);
        e->setAttribute ("reso", l.reso);
        e->setAttribute ("filter", (int) l.filter);
        e->setAttribute ("reverse", l.reverse);
        e->setAttribute ("mode", (int) l.mode);
        e->setAttribute ("midiCh", l.midiChannel);
        e->setAttribute ("midiNote", l.midiNote);
        e->setAttribute ("lockSample", l.lockSample);
        e->setAttribute ("lockPattern", l.lockPattern);
        e->setAttribute ("swing", l.swing);
        e->setAttribute ("humanize", l.humanize);
        e->setAttribute ("mute", l.mute);
        e->setAttribute ("solo", l.solo);
        e->setAttribute ("choke", l.choke);
        e->setAttribute ("out", l.out);
        if (l.follow >= 0)
        {
            e->setAttribute ("follow", l.follow);
            e->setAttribute ("fChance", l.followChance);
            e->setAttribute ("fDelay", l.followDelay);
            e->setAttribute ("fDrift", l.followDrift);
            e->setAttribute ("fVel", l.followVel);
        }
        e->setAttribute ("folder", l.folder.getFullPathName());
        e->setAttribute ("file", l.sample != nullptr ? l.sample->file.getFullPathName() : l.pendingFile.getFullPathName());
        e->setAttribute ("steps", stepsToString (l));
    }
    return xmlPtr;
}

void GrooveboxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateXml())
        copyXmlToBinary (*xml, destData);
}

void GrooveboxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    lastSessionPending = false; // a project's own state always wins over the last session
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateXml (*xml);
}

void GrooveboxProcessor::loadLastSessionIfNew()
{
    if (! lastSessionPending.exchange (false))
        return;
    loadSetup (lastSessionFile());
}

void GrooveboxProcessor::applyStateXml (const juce::XmlElement& xmlRef)
{
    const auto* xml = &xmlRef;
    if (! xml->hasTagName ("Groovebox"))
        return;

    ++stateGeneration; // results of folder scans started before this load are dropped
    for (auto& t : laneToken) ++t;
    ++libraryToken;

    if (auto* params = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    int genre = xml->getIntAttribute ("genre", gen.genre);
    if (xml->getIntAttribute ("version", 1) < 3 && genre >= 10)
        genre += 6; // Hybrid and Free moved when six genres were added in 0.3
    if (xml->getIntAttribute ("version", 1) < 4 && genre >= 16)
        genre += 3; // Hybrid, Free and Reference moved when three foliage genres were added in 0.7
    gen.genre     = juce::jlimit (0, (int) NumGenres - 1, genre);
    gen.bars      = juce::jlimit (1, 64, xml->getIntAttribute ("bars", 1));
    previewBpm    = juce::jlimit (40.0, 300.0, xml->getDoubleAttribute ("previewBpm", genreBpm (gen.genre)));
    songMode      = xml->getBoolAttribute ("songMode", false);
    separateOuts  = xml->getBoolAttribute ("separateOuts", false);
    pianoRollMode = false; // the one-channel piano roll workflow was replaced by per-lane export in 0.6
    songSections.clear();
    {
        juce::StringArray items;
        items.addTokens (xml->getStringAttribute ("songSections"), ";", "");
        for (const auto& it : items)
        {
            juce::StringArray f;
            f.addTokens (it, ":", "");
            if (f.size() >= 4) songSections.push_back ({ f[0], f[1].getIntValue(), f[2].getIntValue(), f[3].getIntValue() });
        }
        juce::StringArray masks;
        masks.addTokens (xml->getStringAttribute ("songMask"), ",", "");
        const juce::SpinLock::ScopedLockType sl (lock);
        songBars = juce::jlimit (16, 256, xml->getIntAttribute ("songBars", 64));
        songMask.fill (0xffffffffu);
        for (int b = 0; b < masks.size() && b < (int) songMask.size(); ++b)
            songMask[(size_t) b] = (juce::uint32) masks[b].getHexValue64();
    }
    gen.density   = (float) xml->getDoubleAttribute ("density", gen.density);
    gen.variation = (float) xml->getDoubleAttribute ("variation", gen.variation);
    gen.irregular = (float) xml->getDoubleAttribute ("irregular", gen.irregular);
    mutateAmount  = (float) xml->getDoubleAttribute ("mutate", mutateAmount);
    seed          = (juce::uint32) xml->getStringAttribute ("seed", "1").getLargeIntValue();
    libraryRoot   = juce::File (xml->getStringAttribute ("library"));
    exportBars    = juce::jlimit (1, 64, xml->getIntAttribute ("exportBars", 4));
    fitFilter     = xml->getBoolAttribute ("fitFilter", false);
    {
        cg::Groove g;
        if (xml->hasAttribute ("grooveData"))
        {
            g.name = xml->getStringAttribute ("grooveName");
            g.bpm  = xml->getDoubleAttribute ("grooveBpm", 0.0);
            g.bars = xml->getIntAttribute ("grooveBars", 0);
            grooveFromString (g, xml->getStringAttribute ("grooveData"));
        }
        ++grooveToken;
        const juce::SpinLock::ScopedLockType sl (lock);
        groove = g;
        grooveAmount    = juce::jlimit (0.0f, 1.0f, (float) xml->getDoubleAttribute ("grooveAmount", 0.8));
        grooveFile      = juce::File (xml->getStringAttribute ("grooveFile"));
        grooveForcedBpm = xml->getDoubleAttribute ("grooveForcedBpm", 0.0);
    }

    struct Pending { juce::File folder, file; };
    std::vector<Pending> pending;
    std::vector<cg::SamplePtr> oldSamples;
    std::vector<juce::Array<juce::File>> oldFiles ((size_t) kMaxLanes);

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (size_t i = 0; i < lanes.size(); ++i)
        {
            auto& l = lanes[i];
            if (l.sample != nullptr) oldSamples.push_back (l.sample);
            l.files.swapWith (oldFiles[i]); // big lists are freed after the lock is released
            l = Lane {};
        }

        int li = 0;
        for (auto* e : xml->getChildWithTagNameIterator ("Lane"))
        {
            if (li >= kMaxLanes) break;
            auto& l = lanes[(size_t) li];
            l.role        = (Role) juce::jlimit (0, kNumRoles - 1, e->getIntAttribute ("role"));
            l.name        = e->getStringAttribute ("name", roleName (l.role));
            l.length      = juce::jlimit (1, kMaxSteps, e->getIntAttribute ("length", 16));
            l.rate        = juce::jlimit (0, (int) rates().size() - 1, e->getIntAttribute ("rate", kRate16));
            l.decay       = (float) e->getDoubleAttribute ("decay", 1.0);
            l.attack      = (float) e->getDoubleAttribute ("attack", 0.0);
            l.reso        = (float) e->getDoubleAttribute ("reso", 0.2);
            l.filter      = (FilterType) juce::jlimit (0, 3, e->getIntAttribute ("filter", 0));
            l.reverse     = e->getBoolAttribute ("reverse");
            l.mode        = LaneMode::Sample; // the FL-channel MIDI mode was retired in 0.3 (FL ignores VST3 MIDI out)
            l.midiChannel = juce::jlimit (1, 16, e->getIntAttribute ("midiCh", 1));
            l.midiNote    = juce::jlimit (0, 127, e->getIntAttribute ("midiNote", 60));
            l.lockSample  = e->getBoolAttribute ("lockSample");
            l.lockPattern = e->getBoolAttribute ("lockPattern");
            l.swing       = (float) e->getDoubleAttribute ("swing");
            l.humanize    = (float) e->getDoubleAttribute ("humanize", 0.15);
            l.mute        = e->getBoolAttribute ("mute");
            l.solo        = e->getBoolAttribute ("solo");
            l.choke       = juce::jlimit (0, 4, e->getIntAttribute ("choke"));
            l.out         = juce::jlimit (-1, kNumOuts - 1, e->getIntAttribute ("out", -1));
            l.follow       = juce::jlimit (-1, kMaxLanes - 1, e->getIntAttribute ("follow", -1));
            l.followChance = (float) e->getDoubleAttribute ("fChance", 0.35);
            l.followDelay  = (float) e->getDoubleAttribute ("fDelay", 40.0);
            l.followDrift  = (float) e->getDoubleAttribute ("fDrift", 0.35);
            l.followVel    = (float) e->getDoubleAttribute ("fVel", 0.5);
            stepsFromString (l, e->getStringAttribute ("steps"));
            pending.push_back ({ juce::File (e->getStringAttribute ("folder")), juce::File (e->getStringAttribute ("file")) });
            ++li;
        }
        numLanes = juce::jmax (1, li);
    }

    for (auto& s : oldSamples)
        retired.push_back (std::move (s));

    // Reconnect sample folders (scanned in the background) and the exact sample that was selected.
    for (int li = 0; li < (int) pending.size(); ++li)
    {
        const auto& p = pending[(size_t) li];
        if (p.folder.isDirectory())
        {
            // Load the chosen sample straight away so it plays even before the folder scan finishes.
            if (p.file.existsAsFile())
            {
                lanes[(size_t) li].files = { p.file };
                loadSampleIndex (li, 0);
            }
            setLaneFolder (li, p.folder, false, p.file);
        }
        else if (p.file.existsAsFile())
        {
            lanes[(size_t) li].files = { p.file };
            loadSampleIndex (li, 0);
            analyzer.enqueue ({ p.file });
        }
    }

    ++modelVersion;
}

//==============================================================================
bool GrooveboxProcessor::LaneSnap::operator== (const LaneSnap& o) const
{
    if (role != o.role || name != o.name || length != o.length || rate != o.rate || choke != o.choke || out != o.out
        || decay != o.decay || attack != o.attack || reso != o.reso || swing != o.swing || humanize != o.humanize
        || level != o.level || pan != o.pan || pitch != o.pitch || cutoff != o.cutoff || filter != o.filter
        || reverse != o.reverse || mute != o.mute || solo != o.solo || lockSample != o.lockSample
        || lockPattern != o.lockPattern || folder != o.folder || file != o.file || steps.size() != o.steps.size()
        || follow != o.follow || fChance != o.fChance || fDelay != o.fDelay || fDrift != o.fDrift || fVel != o.fVel)
        return false;
    for (size_t i = 0; i < steps.size(); ++i)
    {
        const auto& a = steps[i];
        const auto& b = o.steps[i];
        if (a.on != b.on || a.vel != b.vel || a.prob != b.prob || a.nudge != b.nudge || a.ratchet != b.ratchet
            || a.pitch != b.pitch || a.pan != b.pan || a.cutoff != b.cutoff || a.start != b.start)
            return false;
    }
    return true;
}

bool GrooveboxProcessor::Snapshot::operator== (const Snapshot& o) const
{
    if (numLanes != o.numLanes || songMode != o.songMode || songBars != o.songBars || lanes != o.lanes
        || sections.size() != o.sections.size() || mask != o.mask)
        return false;
    for (size_t i = 0; i < sections.size(); ++i)
        if (sections[i].startBar != o.sections[i].startBar || sections[i].bars != o.sections[i].bars || sections[i].type != o.sections[i].type)
            return false;
    return true;
}

GrooveboxProcessor::Snapshot GrooveboxProcessor::takeSnapshot() const
{
    Snapshot sn;
    sn.numLanes = numLanes.load();
    sn.songMode = songMode.load();
    sn.songBars = songBars;
    sn.sections = songSections;
    sn.mask = songMask;
    for (int li = 0; li < sn.numLanes; ++li)
    {
        const auto& l = lanes[(size_t) li];
        LaneSnap ls;
        ls.role = l.role; ls.name = l.name; ls.length = l.length; ls.rate = l.rate; ls.choke = l.choke; ls.out = l.out;
        ls.steps.assign (l.steps.begin(), l.steps.begin() + juce::jlimit (1, kMaxSteps, l.length));
        ls.decay = l.decay; ls.attack = l.attack; ls.reso = l.reso; ls.swing = l.swing; ls.humanize = l.humanize;
        ls.level = pLevel[(size_t) li]->load(); ls.pan = pPan[(size_t) li]->load();
        ls.pitch = pPitch[(size_t) li]->load(); ls.cutoff = pCutoff[(size_t) li]->load();
        ls.filter = l.filter; ls.reverse = l.reverse; ls.mute = l.mute; ls.solo = l.solo;
        ls.lockSample = l.lockSample; ls.lockPattern = l.lockPattern;
        ls.follow = l.follow; ls.fChance = l.followChance; ls.fDelay = l.followDelay; ls.fDrift = l.followDrift; ls.fVel = l.followVel;
        ls.folder = l.folder;
        ls.file = l.sample != nullptr ? l.sample->file : l.pendingFile;
        sn.lanes.push_back (std::move (ls));
    }
    return sn;
}

void GrooveboxProcessor::restoreSnapshot (const Snapshot& sn)
{
    const int nl = juce::jlimit (1, kMaxLanes, sn.numLanes);
    std::vector<int> loadIndex ((size_t) nl, -1);
    std::vector<bool> rescan ((size_t) nl, false);
    std::vector<SamplePtr> dropped;

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int li = 0; li < nl; ++li)
        {
            auto& l = lanes[(size_t) li];
            const auto& ls = sn.lanes[(size_t) li];
            const bool newSlot = li >= numLanes.load();
            if (newSlot) { l.files.clearQuick(); if (l.sample) dropped.push_back (l.sample); l.sample = nullptr; l.fileIndex = -1; }

            l.role = ls.role; l.name = ls.name; l.length = ls.length; l.rate = ls.rate; l.choke = ls.choke; l.out = ls.out;
            for (auto& st : l.steps) st = Step {};
            std::copy (ls.steps.begin(), ls.steps.end(), l.steps.begin());
            l.decay = ls.decay; l.attack = ls.attack; l.reso = ls.reso; l.swing = ls.swing; l.humanize = ls.humanize;
            l.filter = ls.filter; l.reverse = ls.reverse; l.mute = ls.mute; l.solo = ls.solo;
            l.lockSample = ls.lockSample; l.lockPattern = ls.lockPattern;
            l.follow = ls.follow; l.followChance = ls.fChance; l.followDelay = ls.fDelay; l.followDrift = ls.fDrift; l.followVel = ls.fVel;

            const juce::File currentFile = l.sample != nullptr ? l.sample->file : l.pendingFile;
            if (ls.folder != l.folder || newSlot)
                rescan[(size_t) li] = ls.folder != juce::File();
            else if (ls.file != currentFile)
                loadIndex[(size_t) li] = l.files.indexOf (ls.file);
        }
        for (int li = nl; li < kMaxLanes; ++li)
            if (li < numLanes.load() && lanes[(size_t) li].sample != nullptr) // trailing lanes go away
            {
                dropped.push_back (lanes[(size_t) li].sample);
                lanes[(size_t) li].sample = nullptr;
            }
        numLanes = nl;
        songMode = sn.songMode;
        songBars = sn.songBars;
        songMask = sn.mask;
    }
    songSections = sn.sections;
    for (auto& d : dropped) retired.push_back (std::move (d)); // freed later on the message thread

    for (int li = 0; li < nl; ++li)
    {
        const auto& ls = sn.lanes[(size_t) li];
        auto setParam = [this, li] (const char* what, float v)
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (paramId (li, what))))
                if (std::abs (p->convertFrom0to1 (p->getValue()) - v) > 1.0e-5f)
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        setParam ("level", ls.level); setParam ("pan", ls.pan); setParam ("pitch", ls.pitch); setParam ("cutoff", ls.cutoff);

        if (rescan[(size_t) li])
            setLaneFolder (li, ls.folder, false, ls.file);
        else if (loadIndex[(size_t) li] >= 0)
            loadSampleIndex (li, loadIndex[(size_t) li]);
    }

    current = takeSnapshot();
    current.lanes.resize ((size_t) nl);
    for (int li = 0; li < nl; ++li) // the target state, even while a folder is still being rescanned
        current.lanes[(size_t) li] = sn.lanes[(size_t) li];
    hasCurrent = true;
    ++modelVersion;
}

void GrooveboxProcessor::checkpoint()
{
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return; // one drag = one undo step

    auto now = takeSnapshot();
    if (! hasCurrent)
    {
        current = std::move (now);
        hasCurrent = true;
        return;
    }
    if (now == current)
        return;

    undoStack.push_back (std::move (current));
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
    current = std::move (now);
}

bool GrooveboxProcessor::undo()
{
    checkpoint(); // capture anything not yet recorded
    if (undoStack.empty()) return false;
    redoStack.push_back (current);
    auto target = std::move (undoStack.back());
    undoStack.pop_back();
    restoreSnapshot (target);
    return true;
}

bool GrooveboxProcessor::redo()
{
    checkpoint();
    if (redoStack.empty()) return false;
    undoStack.push_back (current);
    auto target = std::move (redoStack.back());
    redoStack.pop_back();
    restoreSnapshot (target);
    return true;
}

//==============================================================================
juce::File GrooveboxProcessor::setupsFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Groovebox").getChildFile ("Setups");
    dir.createDirectory();
    return dir;
}

juce::File GrooveboxProcessor::lastSessionFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Groovebox").getChildFile ("last-session.gbx");
}

bool GrooveboxProcessor::saveSetup (const juce::File& file)
{
    auto xml = createStateXml();
    if (xml == nullptr) return false;
    file.getParentDirectory().createDirectory();
    return xml->writeTo (file);
}

bool GrooveboxProcessor::loadSetup (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("Groovebox"))
        return false;
    applyStateXml (*xml);
    return true;
}

void GrooveboxProcessor::autosave()
{
    if (ioBusy.load() > 0)
        return; // wait until folder scans have finished so nothing half-loaded is saved

    auto xml = createStateXml();
    if (xml == nullptr) return;
    const auto text = xml->toString();
    if (text == lastAutosaved) return;

    auto f = lastSessionFile();
    f.getParentDirectory().createDirectory();
    if (f.replaceWithText (text))
        lastAutosaved = text;
}

//==============================================================================
juce::AudioProcessorEditor* GrooveboxProcessor::createEditor()
{
    return new GrooveboxEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveboxProcessor();
}
